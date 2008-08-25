/*-------------------------------------------------------------------------
 *
 *   FILE
 *	pqxx/connection_base.hxx
 *
 *   DESCRIPTION
 *      definition of the pqxx::connection_base abstract base class.
 *   pqxx::connection_base encapsulates a frontend to backend connection
 *   DO NOT INCLUDE THIS FILE DIRECTLY; include pqxx/connection_base instead.
 *
 * Copyright (c) 2001-2008, Jeroen T. Vermeulen <jtv@xs4all.nl>
 *
 * See COPYING for copyright license.  If you did not receive a file called
 * COPYING with this source code, please notify the distributor of this mistake,
 * or contact the author.
 *
 *-------------------------------------------------------------------------
 */
#ifndef PQXX_H_CONNECTION_BASE
#define PQXX_H_CONNECTION_BASE

#include "pqxx/compiler-public.hxx"
#include "pqxx/compiler-internal-pre.hxx"

#include <map>
#include <memory>

#include "pqxx/except"
#include "pqxx/prepared_statement"
#include "pqxx/util"


/* Use of the libpqxx library starts here.
 *
 * Everything that can be done with a database through libpqxx must go through
 * a connection object derived from connection_base.
 */

/* Methods tested in eg. self-test program test1 are marked with "//[t1]"
 */

namespace pqxx
{
class result;
class transaction_base;
class notify_listener;
class connectionpolicy;

namespace internal
{
class reactivation_avoidance_exemption;
class sql_cursor;

class reactivation_avoidance_counter
{
public:
  reactivation_avoidance_counter() : m_counter(0) {}

  void add(int n) throw () { m_counter += n; }
  void clear() throw () { m_counter = 0; }
  int get() const throw () { return m_counter; }

  void give_to(reactivation_avoidance_counter &rhs) throw ()
  {
    rhs.add(m_counter);
    clear();
  }

private:
  int m_counter;
};

}

/**
 * @addtogroup noticer Error/warning output
 * @{
 */

/// Base class for user-definable error/warning message processor
/** To define a custom method of handling notices, derive a new class from
 * noticer and override the virtual function
 * @code operator()(const char[]) throw() @endcode
 * to process the message passed to it.
 */
struct PQXX_LIBEXPORT PQXX_NOVTABLE noticer :
  PGSTD::unary_function<const char[], void>
{
  noticer(){}		// Silences bogus warning in some gcc versions
  virtual ~noticer() throw () {}
  virtual void operator()(const char Msg[]) throw () =0;
};


/// No-op message noticer; produces no output
struct PQXX_LIBEXPORT nonnoticer : noticer
{
  nonnoticer(){}	// Silences bogus warning in some gcc versions
  virtual void operator()(const char []) throw () {}
};

/**
 * @}
 */

/// Encrypt password for given user.  Requires libpq 8.2 or better.
/** Use this when setting a new password for the user if password encryption is
 * enabled.  Inputs are the username the password is for, and the plaintext
 * password.
 *
 * @return encrypted version of the password, suitable for encrypted PostgreSQL
 * authentication.
 *
 * Thus the password for a user can be changed with:
 * @code
 * void setpw(transaction_base &t, const string &user, const string &pw)
 * {
 *   t.exec("ALTER USER " + user + " "
 *   	"PASSWORD '" + encrypt_password(user,pw) + "'");
 * }
 * @endcode
 *
 * @since libpq 8.2
 */
PGSTD::string PQXX_LIBEXPORT encrypt_password(				//[t0]
	const PGSTD::string &user,
	const PGSTD::string &password);

/// connection_base abstract base class; represents a connection to a database.
/** This is the first class to look at when you wish to work with a database
 * through libpqxx.  Depending on the implementing concrete child class, a
 * connection can be automatically opened when it is constructed, or when it is
 * first used.  The connection is automatically closed upon destruction, if it
 * hasn't already been closed manually.
 *
 * To query or manipulate the database once connected, use one of the
 * transaction classes (see pqxx/transaction_base.hxx) or preferably the
 * transactor framework (see pqxx/transactor.hxx).
 *
 * If a network connection to the database server fails, the connection will be
 * restored automatically (although any transaction going on at the time will
 * have to be aborted).  This also means that any information set in previous
 * transactions that is not stored in the database, such as temp tables or
 * connection-local variables defined with PostgreSQL's SET command, will be
 * lost.  Whenever you create such state, either keept it local to one
 * transaction, where possible, or inhibit automatic reactivation of the
 * connection using the inhibit_reactivation() method.
 *
 * When a connection breaks, you will typically get a broken_connection
 * exception.  This can happen at almost any point, and the details may depend
 * on which connection class (all derived from this one) you use.
 *
 * As a general rule, always avoid raw queries if libpqxx offers a dedicated
 * function for the same purpose.  There may be hidden logic to hide certain
 * complications from you, such as reinstating session variables when a
 * broken or disabled connection is reactivated.
 *
 * @warning On Unix-like systems, including GNU and BSD systems, your program
 * may receive the SIGPIPE signal when the connection to the backend breaks.  By
 * default this signal will abort your program.  Use "signal(SIGPIPE, SIG_IGN)"
 * if you want your program to continue running after a connection fails.
 */
class PQXX_LIBEXPORT connection_base
{
public:
  /// Explicitly close connection.
  void disconnect() throw ();						//[t2]

   /// Is this connection open at the moment?
  /** @warning This function is @b not needed in most code.  Resist the
   * temptation to check it after opening a connection; instead, rely on the
   * broken_connection exception that will be thrown on connection failure.
   */
  bool is_open() const throw ();					//[t1]

 /**
   * @name Activation
   *
   * Connections can be temporarily deactivated, or they can break because of
   * overly impatient firewalls dropping TCP connections.  Where possible,
   * libpqxx will try to re-activate these when resume using them, or you can
   * wake them up explicitly.  You probably won't need this feature, but you
   * should be aware of it.
   */
  //@{
  /// Explicitly activate deferred or deactivated connection.
  /** Use of this method is entirely optional.  Whenever a connection is used
   * while in a deferred or deactivated state, it will transparently try to
   * bring itself into an activated state.  This function is best viewed as an
   * explicit hint to the connection that "if you're not in an active state, now
   * would be a good time to get into one."  Whether a connection is currently
   * in an active state or not makes no real difference to its functionality.
   * There is also no particular need to match calls to activate() with calls to
   * deactivate().  A good time to call activate() might be just before you
   * first open a transaction on a lazy connection.
   */
  void activate();							//[t12]

  /// Explicitly deactivate connection.
  /** Like its counterpart activate(), this method is entirely optional.
   * Calling this function really only makes sense if you won't be using this
   * connection for a while and want to reduce the number of open connections on
   * the database server.
   * There is no particular need to match or pair calls to deactivate() with
   * calls to activate(), but calling deactivate() during a transaction is an
   * error.
   */
  void deactivate();							//[t12]

  /// Disallow (or permit) connection recovery
  /** A connection whose underlying socket is not currently connected to the
   * server will normally (re-)establish communication with the server whenever
   * needed, or when the client program requests it (although for reasons of
   * integrity, never inside a transaction; but retrying the whole transaction
   * may implicitly cause the connection to be restored).  In normal use this is
   * quite a convenient thing to have and presents a simple, safe, predictable
   * interface.
   *
   * There is at least one situation where this feature is not desirable,
   * however.  Although most session state (prepared statements, session
   * variables) is automatically restored to its working state upon connection
   * reactivation, temporary tables and so-called WITH HOLD cursors (which can
   * live outside transactions) are not.
   *
   * Cursors that live outside transactions are automatically handled, and the
   * library will quietly ignore requests to deactivate or reactivate
   * connections while they exist; it does not want to give you the illusion of
   * being back in your transaction when in reality you just dropped a cursor.
   * With temporary tables this is not so easy: there is no easy way for the
   * library to detect their creation or track their lifetimes.
   *
   * So if your program uses temporary tables, and any part of this use happens
   * outside of any database transaction (or spans multiple transactions), some
   * of the work you have done on these tables may unexpectedly be undone if the
   * connection is broken or deactivated while any of these tables exists, and
   * then reactivated or implicitly restored before you are finished with it.
   *
   * If this describes any part of your program, guard it against unexpected
   * reconnections by inhibiting reconnection at the beginning.  And if you want
   * to continue doing work on the connection afterwards that no longer requires
   * the temp tables, you can permit it again to get the benefits of connection
   * reactivation for the remainder of the program.
   *
   * @param inhibit should reactivation be inhibited from here on?
   *
   * @warning Some connection types (the lazy and asynchronous types) defer
   * completion of the socket-level connection until it is actually needed by
   * the client program.  Inhibiting reactivation before this connection is
   * really established will prevent these connection types from doing their
   * work.  For those connection types, if you are sure that reactivation needs
   * to be inhibited before any query goes across the connection, activate() the
   * connection first.  This will ensure that definite activation happens before
   * you inhibit it.
   */
  void inhibit_reactivation(bool inhibit)				//[t86]
	{ m_inhibit_reactivation=inhibit; }

  /// Make the connection fail.  @warning Do not use this except for testing!
  /** Breaks the connection in some unspecified, horrible, dirty way to enable
   * failure testing.
   *
   * Do not use this in normal programs.  This is only meant for testing.
   */
  void simulate_failure();						//[t94]
  //@}

  /**
   * @name Error/warning output
   *
   * Whenever the database has a warning or error to report, it will call a
   * @e noticer to process the associated message.  The default noticer sends
   * the text of the message to standard error output, but you may choose to
   * select a different noticer for the connection.
   */
  //@{

  /// Set handler for postgresql errors or warning messages.
  /** The use of auto_ptr implies ownership, so unless the returned value is
   * copied to another auto_ptr, it will be deleted directly after the call.
   * This may be important when running under Windows, where a DLL cannot free
   * memory allocated by the main program.  The auto_ptr will delete the object
   * from your code context, rather than from inside the library.
   *
   * If a noticer exists when the connection_base is destructed, it will also be
   * deleted.
   *
   * @param N New message handler; must not be null or equal to the old one
   * @return Previous handler
   */
  PGSTD::auto_ptr<noticer> set_noticer(PGSTD::auto_ptr<noticer> N)
    throw ();								//[t14]
  noticer *get_noticer() const throw () { return m_Noticer.get(); }	//[t14]

  /// Invoke notice processor function.  The message should end in newline.
  void process_notice(const char[]) throw ();				//[t14]
  /// Invoke notice processor function.  Newline at end is recommended.
  void process_notice(const PGSTD::string &) throw ();			//[t14]

  //@}

  /// Enable tracing to a given output stream, or NULL to disable.
  void trace(FILE *) throw ();						//[t3]

  /**
   * @name Connection properties
   *
   * These are probably not of great interest, since most are derived from
   * information supplied by the client program itself, but they are included
   * for completeness.
   */
  //@{
  /// Name of database we're connected to, if any.
  /** @warning This activates the connection, which may fail with a
   * broken_connection exception.
   */
  const char *dbname();							//[t1]

  /// Database user ID we're connected under, if any.
  /** @warning This activates the connection, which may fail with a
   * broken_connection exception.
   */
  const char *username();						//[t1]

  /// Address of server, or NULL if none specified (i.e. default or local)
  /** @warning This activates the connection, which may fail with a
   * broken_connection exception.
   */
  const char *hostname();						//[t1]

  /// Server port number we're connected to.
  /** @warning This activates the connection, which may fail with a
   * broken_connection exception.
   */
  const char *port();							//[t1]

  /// Process ID for backend process.
  /** Use with care: connections may be lost and automatically re-established
   * without your knowledge, in which case this process ID may no longer be
   * correct.  You may, however, assume that this number remains constant and
   * reliable within the span of a successful backend transaction.  If the
   * transaction fails, which may be due to a lost connection, then this number
   * will have become invalid at some point within the transaction.
   *
   * @return Process identifier, or 0 if not currently connected.
   */
  int backendpid() const throw ();					//[t1]

  /// Socket currently used for connection, or -1 for none.  Use with care!
  /** Query the current socket number.  This is intended for event loops based
   * on functions such as select() or poll(), where multiple file descriptors
   * are watched.
   *
   * Please try to stay away from this function.  It is really only meant for
   * event loops that need to wait on more than one file descriptor.  If all you
   * need is to block until a notification arrives, for instance, use
   * await_notification().  If you want to issue queries and retrieve results in
   * nonblocking fashion, check out the pipeline class.
   *
   * @warning Don't store this value anywhere, and always be prepared for the
   * possibility that there is no socket.  The socket may change or even go away
   * during any invocation of libpqxx code, no matter how trivial.
   */
  int sock() const throw ();						//[t87]

  /** 
   * @name Capabilities
   *
   * Some functionality is only available in certain versions of the backend,
   * or only when speaking certain versions of the communications protocol that
   * connects us to the backend.  This includes clauses for SQL statements that
   * were not accepted in older database versions, but are required in newer
   * versions to get the same behaviour.
   */
  //@{
 
  /// Session capabilities
  enum capability
  {
    /// Does the backend support prepared statements?  (If not, we emulate them)
    cap_prepared_statements,

    /// Can we specify WITH OIDS with CREATE TABLE?  If we can, we should.
    cap_create_table_with_oids,

    /// Can transactions be nested in other transactions?
    cap_nested_transactions,

    /// Can cursors be declared SCROLL?
    cap_cursor_scroll,
    /// Can cursors be declared WITH HOLD?
    cap_cursor_with_hold,
    /// Can cursors be updateable?
    cap_cursor_update,

    /// Can we ask what table column a result column came from?
    cap_table_column,

    /// Not a capability value; end-of-enumeration marker
    cap_end
  };


  /// Does this connection seem to support the given capability?
  /** Don't try to be smart by caching this information anywhere.  Obtaining it
   * is quite fast (especially after the first time) and what's more, a
   * capability may "suddenly" appear or disappear if the connection is broken
   * or deactivated, and then restored.  This may happen silently any time no
   * backend transaction is active; if it turns out that the server was upgraded
   * or restored from an older backup, or the new connection goes to a different
   * backend, then the restored session may have different capabilities than
   * were available previously.
   *
   * Some guesswork is involved in establishing the presence of any capability;
   * try not to rely on this function being exactly right.
   *
   * @warning Make sure your connection is active before calling this function,
   * or the answer will always be "no."  In particular, if you are using this
   * function on a newly-created lazyconnection, activate the connection first.
   */
  bool supports(capability c) const throw () { return m_caps[c]; }	//[t88]

  /// What version of the PostgreSQL protocol is this connection using?
  /** The answer can be 0 (when there is no connection, or the libpq version
   * being used is too old to obtain the information); 2 for protocol 2.0; 3 for
   * protocol 3.0; and possibly higher values as newer protocol versions are
   * taken into use.
   *
   * If the connection is broken and restored, the restored connection could
   * possibly a different server and protocol version.  This would normally
   * happen if the server is upgraded without shutting down the client program,
   * for example.
   *
   * Requires libpq version from PostgreSQL 7.4 or better.
   */
  int protocol_version() const throw ();				//[t1]

  /// What version of the PostgreSQL server are we connected to?
  /** The result is a bit complicated: each of the major, medium, and minor
   * release numbers is written as a two-digit decimal number, and the three
   * are then concatenated.  Thus server version 7.4.2 will be returned as the
   * decimal number 70402.  If there is no connection to the server, of if the
   * libpq version is too old to obtain the information, zero is returned.
   *
   * @warning When writing version numbers in your code, don't add zero at the
   * beginning!  Numbers beginning with zero are interpreted as octal (base-8)
   * in C++.  Thus, 070402 is not the same as 70402, and 080000 is not a number
   * at all because there is no digit "8" in octal notation.  Use strictly
   * decimal notation when it comes to these version numbers.
   */
  int server_version() const throw ();					//[t1]
  //@}

  /// Set client-side character encoding
  /** Search the PostgreSQL documentation for "multibyte" or "character set
   * encodings" to find out more about the available encodings, how to extend
   * them, and how to use them.  Not all server-side encodings are compatible
   * with all client-side encodings or vice versa.
   * @param Encoding Name of the character set encoding to use
   */
  void set_client_encoding(const PGSTD::string &Encoding)		//[t7]
	{ set_variable("CLIENT_ENCODING", Encoding); }

  /// Set session variable
  /** Set a session variable for this connection, using the SET command.  If the
   * connection to the database is lost and recovered, the last-set value will
   * be restored automatically.  See the PostgreSQL documentation for a list of
   * variables that can be set and their permissible values.
   * If a transaction is currently in progress, aborting that transaction will
   * normally discard the newly set value.  Known exceptions are nontransaction
   * (which doesn't start a real backend transaction) and PostgreSQL versions
   * prior to 7.3.
   * @warning Do not mix the set_variable interface with manual setting of
   * variables by executing the corresponding SQL commands, and do not get or
   * set variables while a tablestream or pipeline is active on the same
   * connection.
   * @param Var Variable to set
   * @param Value Value vor Var to assume: an identifier, a quoted string, or a
   * number.
   */
  void set_variable(const PGSTD::string &Var,
		    const PGSTD::string &Value);			//[t60]

  /// Read session variable
  /** Will try to read the value locally, from the list of variables set with
   * the set_variable function.  If that fails, the database is queried.
   * @warning Do not mix the set_variable interface with manual setting of
   * variables by executing the corresponding SQL commands, and do not get or
   * set variables while a tablestream or pipeline is active on the same
   * connection.
   */
  PGSTD::string get_variable(const PGSTD::string &);			//[t60]
  //@}


  /**
   * @name Notifications and Listeners
   */
  //@{
  /// Check for pending notifications and take appropriate action.
  /**
   * All notifications found pending at call time are processed by finding
   * any matching listeners and invoking those.  If no listeners matched the
   * notification string, none are invoked but the notification is considered
   * processed.
   *
   * Exceptions thrown by client-registered listeners handlers are reported
   * using the connection's message noticer, but the exceptions themselves are
   * not passed on outside this function.
   *
   * @return Number of notifications processed
   */
  int get_notifs();							//[t4]


  /// Wait for a notification to come in
  /** The wait may also be terminated by other events, such as the connection
   * to the backend failing.  Any pending or received notifications are
   * processed as part of the call.
   *
   * @return Number of notifications processed
   */
  int await_notification();						//[t78]

  /// Wait for a notification to come in, or for given timeout to pass
  /** The wait may also be terminated by other events, such as the connection
   * to the backend failing.  Any pending or received notifications are
   * processed as part of the call.

   * @return Number of notifications processed
   */
  int await_notification(long seconds, long microseconds);		//[t79]
  //@}


  /**
   * @name Prepared statements
   *
   * PostgreSQL supports prepared SQL statements, i.e. statements that can be
   * registered under a client-provided name, optimized once by the backend, and
   * executed any number of times under the given name.
   *
   * Prepared statement definitions are not sensitive to transaction boundaries;
   * a statement defined inside a transaction will remain defined outside that
   * transaction, even if the transaction itself is subsequently aborted.  Once
   * a statement has been prepared, only closing the connection or explicitly
   * "unpreparing" it can make it go away.
   *
   * Use the transaction classes' exec_prepared() functions to execute a
   * prepared statement.
   *
   * @warning Prepared statements are not necessarily defined on the backend
   * right away; they may be cached by libpqxx.  This means that statements may
   * be prepared before the connection is fully established, and that it's
   * relatively cheap to pre-prepare lots of statements that may or may not be
   * used during the session.  It also means, however, that errors in the
   * prepared statement may not show up until it is first used.  Such failure
   * may cause the current transaction to roll back.
   *
   * @warning Never try to prepare, execute, or unprepare a prepared statement
   * manually using direct SQL queries.  Always use the functions provided by
   * libpqxx.
   *
   * @{
   */

  /// Define a prepared statement
  /** To declare parameters to this statement, add them by calling the function
   * invocation operator (@c operator()) on the returned object.  See
   * prepare_param_declaration and prepare::param_treatment for more about how
   * to do this.
   *
   * The statement's definition can refer to a parameter using the parameter's
   * positional number n in the definition.  For example, the first parameter
   * can be used as a variable "$1", the second as "$2" and so on.
   *
   * One might use a prepared statement as in the following example.  Note the
   * unusual syntax associated with parameter definitions and parameter passing:
   * every new parameter is just a parenthesized expression that is simply
   * tacked onto the end of the statement!
   *
   * @code
   * using namespace pqxx;
   * void foo(connection_base &C)
   * {
   *   C.prepare("findtable",
   *             "select * from pg_tables where name=$1")
   *             ("varchar", treat_string);
   *   work W(C);
   *   result R = W.prepared("findtable")("mytable").exec();
   *   if (R.empty()) throw runtime_error("mytable not found!");
   * }
   * @endcode
   *
   * For better performance, prepared statements aren't really registered with
   * the backend until they are first used.  If this is not what you want, e.g.
   * because you have very specific realtime requirements, you can use the
   * @c prepare_now() function to force immediate preparation.
   *
   * @warning The statement may not be registered with the backend until it is
   * actually used.  So if the statement is syntactically incorrect, for
   * example, a syntax_error may be thrown either from here, or when you try to
   * call the statement later, or somewhere inbetween if prepare_now() is
   * called.  This is not guaranteed, and it's still possible to get a
   * broken_connection or sql_error here, for example.
   *
   * @param name unique identifier to associate with new prepared statement
   * @param definition SQL statement to prepare
   */
  prepare::declaration prepare(const PGSTD::string &name,
	const PGSTD::string &definition);				//[t85]

  /// Drop prepared statement
  void unprepare(const PGSTD::string &name);				//[t85]

  /// Request that prepared statement be registered with the server
  /** If the statement had already been fully prepared, this will do nothing.
   *
   * If the connection should break and be transparently restored, then the new
   * connection will again defer registering the statement with the server.
   * Since connections are never restored inside backend transactions, doing
   * this once at the beginning of your transaction ensures that the statement
   * will not be re-registered during that transaction.  In most cases, however,
   * it's probably better not to use this and let the connection decide when and
   * whether to register prepared statements that you've defined.
   */
  void prepare_now(const PGSTD::string &name);				//[t85]

  /**
   * @}
   */


  /**
   * @name Transactor framework
   *
   * See the transactor class template for more about transactors.  To use the
   * transactor framework, encapsulate your transaction code in a class derived
   * from an instantiation of the pqxx::transactor template.  Then, to execute
   * it, create an object of your transactor class and pass it to one of the
   * perform() functions here.
   *
   * The perform() functions may create and execute several copies of the
   * transactor before succeeding or ultimately giving up.  If there is any
   * doubt over whether execution succeeded (this can happen if the connection
   * to the server is lost just before the backend can confirm success), it is
   * no longer retried and an in_doubt_error is thrown.
   *
   * Take care: no member functions will ever be invoked on the transactor
   * object you pass into perform().  The object you pass in only serves as a
   * "prototype" for the job to be done.  The perform() function will
   * copy-construct transactors from the original you passed in, executing the
   * copies only.  The original object remains "clean" in its original state.
   *
   * @{
   */

  /// Perform the transaction defined by a transactor-based object.
  /**
   * Invokes the given transactor, making at most Attempts attempts to perform
   * the encapsulated code.  If the code throws any exception other than
   * broken_connection, it will be aborted right away.
   *
   * @param T The transactor to be executed.
   * @param Attempts Maximum number of attempts to be made to execute T.
   */
  template<typename TRANSACTOR>
  void perform(const TRANSACTOR &T, int Attempts);			//[t4]

  /// Perform the transaction defined by a transactor-based object.
  /**
   * @param T The transactor to be executed.
   */
  template<typename TRANSACTOR>
  void perform(const TRANSACTOR &T) { perform(T, 3); }

  /**
   * @}
   */

  /// Suffix unique number to name to make it unique within session context
  /** Used internally to generate identifiers for SQL objects (such as cursors
   * and nested transactions) based on a given human-readable base name.
   */
  PGSTD::string adorn_name(const PGSTD::string &);			//[90]

  /**
   * @addtogroup escaping String escaping
   *
   * Use these functions to "groom" user-provided strings before using them in
   * your SQL statements.  This reduces the chance of failures when users type
   * unexpected characters, but more importantly, it helps prevent so-called SQL
   * injection attacks.
   *
   * To understand what SQL injection vulnerabilities are and why they should be
   * prevented, imagine you use the following SQL statement somewhere in your
   * program:
   *
   * @code
   *	TX.exec("SELECT number,amount "
   *		"FROM accounts "
   *		"WHERE allowed_to_see('" + userid + "','" + password + "')");
   * @endcode
   *
   * This shows a logged-in user important information on all accounts he is
   * authorized to view.  The userid and password strings are variables entered
   * by the user himself.
   *
   * Now, if the user is actually an attacker who knows (or can guess) the
   * general shape of this SQL statement, imagine he enters the following
   * password:
   *
   * @code
   *	x') OR ('x' = 'x
   * @endcode
   *
   * Does that make sense to you?  Probably not.  But if this is inserted into
   * the SQL string by the C++ code above, the query becomes:
   *
   * @code
   *	SELECT number,amount
   *	FROM accounts
   *	WHERE allowed_to_see('user','x') OR ('x' = 'x')
   * @endcode
   *
   * Is this what you wanted to happen?  Probably not!  The neat
   * allowed_to_see() clause is completely circumvented by the
   * "<tt>OR ('x' = 'x')</tt>" clause, which is always @c true.  Therefore, the
   * attacker will get to see all accounts in the database!
   *
   * To prevent this from happening, use the transaction's esc() function:
   *
   * @code
   *	TX.exec("SELECT number,amount "
   *		"FROM accounts "
   *		"WHERE allowed_to_see('" + TX.esc(userid) + "', "
   *			"'" + TX.esc(password) + "')");
   * @endcode
   *
   * Now, the quotes embedded in the attacker's string will be neatly escaped so
   * they can't "break out" of the quoted SQL string they were meant to go into:
   *
   * @code
   *	SELECT number,amount
   *	FROM accounts
   *	WHERE allowed_to_see('user', 'x'') OR (''x'' = ''x')
   * @endcode
   *
   * If you look carefully, you'll see that thanks to the added escape
   * characters (a single-quote is escaped in SQL by doubling it) all we get is
   * a very strange-looking password string--but not a change in the SQL
   * statement.
   */
  //@{
  /// Escape string for use as SQL string literal on this connection
  PGSTD::string esc(const char str[]);

  /// Escape string for use as SQL string literal on this connection
  PGSTD::string esc(const char str[], size_t maxlen);

  /// Escape string for use as SQL string literal on this connection
  PGSTD::string esc(const PGSTD::string &str);

  /// Escape binary string for use as SQL string literal on this connection
  PGSTD::string esc_raw(const unsigned char str[], size_t len);

  /// Represent object as SQL string, including quoting & escaping.
  /** Nulls are recognized and represented as SQL nulls. */
  template<typename T>
  PGSTD::string quote(const T &t)
  {
    if (string_traits<T>::is_null(t)) return "NULL";
    return "'" + this->esc(to_string(t)) + "'";
  }
  //@}

protected:
  explicit connection_base(connectionpolicy &);
  void init();

  void close() throw ();
  void wait_read() const;
  void wait_read(long seconds, long microseconds) const;
  void wait_write() const;

private:
  void PQXX_PRIVATE clearcaps() throw ();
  void PQXX_PRIVATE SetupState();
  void PQXX_PRIVATE check_result(const result &);

  void PQXX_PRIVATE InternalSetTrace() throw ();
  int PQXX_PRIVATE Status() const throw ();
  const char *ErrMsg() const throw ();
  void PQXX_PRIVATE Reset();
  void PQXX_PRIVATE RestoreVars();
  PGSTD::string PQXX_PRIVATE RawGetVar(const PGSTD::string &);
  void PQXX_PRIVATE process_notice_raw(const char msg[]) throw ();
  void switchnoticer(const PGSTD::auto_ptr<noticer> &) throw ();

  void read_capabilities() throw ();

  friend class subtransaction;
  void set_capability(capability) throw ();

  prepare::internal::prepared_def &find_prepared(const PGSTD::string &);

  friend class prepare::declaration;
  void prepare_param_declare(const PGSTD::string &statement,
      const PGSTD::string &sqltype,
      prepare::param_treatment);

  prepare::internal::prepared_def &register_prepared(const PGSTD::string &);
  result prepared_exec(const PGSTD::string &,
	const char *const[],
	const int[],
	int);

  friend class arrayvalue;
  int PQXX_PRIVATE encoding_code() throw ();

  /// Connection handle
  internal::pq::PGconn *m_Conn;

  connectionpolicy &m_policy;

  /// Have we successfully established this connection?
  bool m_Completed;

  /// Active transaction on connection, if any
  internal::unique<transaction_base> m_Trans;

  /// User-defined notice processor, if any
  PGSTD::auto_ptr<noticer> m_Noticer;

  /// Default notice processor
  /** We must restore the notice processor to this default after removing our
   * own noticers.  Failure to do so caused test005 to crash on some systems.
   * Kudos to Bart Samwel for tracking this down and submitting the fix!
   */
  internal::pq::PQnoticeProcessor m_defaultNoticeProcessor;

  /// File to trace to, if any
  FILE *m_Trace;

  typedef PGSTD::multimap<PGSTD::string, pqxx::notify_listener *> listenerlist;
  /// Notifications this session is listening on
  listenerlist m_listeners;

  /// Variables set in this session
  PGSTD::map<PGSTD::string, PGSTD::string> m_Vars;

  typedef PGSTD::map<PGSTD::string, prepare::internal::prepared_def> PSMap;

  /// Prepared statements existing in this section
  PSMap m_prepared;

  /// Server version
  int m_serverversion;

  /// Set of session capabilities
  bool m_caps[cap_end];

  /// Is reactivation currently inhibited?
  bool m_inhibit_reactivation;

  /// Stacking counter: known objects that can't be auto-reactivated
  internal::reactivation_avoidance_counter m_reactivation_avoidance;

  /// Unique number to use as suffix for identifiers (see adorn_name())
  int m_unique_id;

  friend class transaction_base;
  result PQXX_PRIVATE Exec(const char[], int Retries);
  result pq_exec_prepared(const PGSTD::string &, int, const char *const *);
  void PQXX_PRIVATE RegisterTransaction(transaction_base *);
  void PQXX_PRIVATE UnregisterTransaction(transaction_base *) throw ();
  void PQXX_PRIVATE MakeEmpty(result &);
  bool PQXX_PRIVATE ReadCopyLine(PGSTD::string &);
  void PQXX_PRIVATE WriteCopyLine(const PGSTD::string &);
  void PQXX_PRIVATE EndCopyWrite();
  void PQXX_PRIVATE start_exec(const PGSTD::string &);
  internal::pq::PGresult *get_result();

  void PQXX_PRIVATE RawSetVar(const PGSTD::string &, const PGSTD::string &);
  void PQXX_PRIVATE AddVariables(const PGSTD::map<PGSTD::string,
      PGSTD::string> &);

  friend class largeobject;
  internal::pq::PGconn *RawConnection() const { return m_Conn; }

  friend class notify_listener;
  void add_listener(notify_listener *);
  void remove_listener(notify_listener *) throw ();

  friend class pipeline;
  bool PQXX_PRIVATE consume_input() throw ();
  bool PQXX_PRIVATE is_busy() const throw ();

  friend class internal::sql_cursor;
  friend class dbtransaction;
  friend class internal::reactivation_avoidance_exemption;

  // Not allowed:
  connection_base(const connection_base &);
  connection_base &operator=(const connection_base &);
};



/// Temporarily set different noticer for connection, then restore old one
/** Set different noticer in given connection for the duration of the
 * scoped_noticer's lifetime.  After that, the original noticer is restored.
 *
 * No effort is made to respect any new noticer that may have been set in the
 * meantime, so don't do that.
 */
class PQXX_LIBEXPORT scoped_noticer
{
public:
  /// Start period where different noticer applies to connection
  /**
   * @param c connection object whose noticer should be temporarily changed
   * @param t temporary noticer object to use; will be destroyed on completion
   */
  scoped_noticer(connection_base &c, PGSTD::auto_ptr<noticer> t) throw () :
    m_c(c), m_org(c.set_noticer(t)) { }

  ~scoped_noticer() { m_c.set_noticer(m_org); }

protected:
  /// Take ownership of given noticer, and start using it
  /** This constructor is not public because its interface does not express the
   * fact that the scoped_noticer takes ownership of the noticer through an
   * @c auto_ptr.
   */
  scoped_noticer(connection_base &c, noticer *t) throw () :
    m_c(c),
    m_org()
  {
    PGSTD::auto_ptr<noticer> x(t);
    PGSTD::auto_ptr<noticer> y(c.set_noticer(x));
    m_org = y;
  }

private:
  connection_base &m_c;
  PGSTD::auto_ptr<noticer> m_org;

  /// Not allowed
  scoped_noticer();
  scoped_noticer(const scoped_noticer &);
  scoped_noticer operator=(const scoped_noticer &);
};


/// Temporarily disable the notice processor
class PQXX_LIBEXPORT disable_noticer : scoped_noticer
{
public:
  explicit disable_noticer(connection_base &c) :
    scoped_noticer(c, new nonnoticer) {}
};


namespace internal
{

/// Scoped exemption to reactivation avoidance
class PQXX_LIBEXPORT reactivation_avoidance_exemption
{
public:
  explicit reactivation_avoidance_exemption(connection_base &C) :
    m_home(C),
    m_count(C.m_reactivation_avoidance.get()),
    m_open(C.is_open())
  {
    C.m_reactivation_avoidance.clear();
  }

  ~reactivation_avoidance_exemption()
  {
    // Don't leave connection open if reactivation avoidance is in effect and
    // the connection needed to be reactivated temporarily.
    if (m_count && !m_open) m_home.deactivate();
    m_home.m_reactivation_avoidance.add(m_count);
  }

  void close_connection() throw () { m_open = false; }

private:
  connection_base &m_home;
  int m_count;
  bool m_open;
};


void wait_read(const internal::pq::PGconn *);
void wait_read(const internal::pq::PGconn *, long seconds, long microseconds);
void wait_write(const internal::pq::PGconn *);
} // namespace pqxx::internal


} // namespace pqxx

#include "pqxx/compiler-internal-post.hxx"

#endif

