#include <iostream>
#include <stdexcept>
#include <vector>

#include <pqxx/connection.h>
#include <pqxx/tablereader.h>
#include <pqxx/tablewriter.h>
#include <pqxx/transaction.h>

using namespace PGSTD;
using namespace pqxx;


// Test program for libpqxx.  Copy a table from one database connection to 
// another using a TableReader and a TableWriter.  Any data already in the
// destination table is overwritten.  Lazy connections are used.
//
// Usage: test25 [connect-string] [orgtable] [dsttable]
//
// Where the connect-string is a set of connection options in Postgresql's
// PQconnectdb() format, eg. "dbname=template1" to select from a database
// called template1, or "host=foo.bar.net user=smith" to connect to a backend 
// running on host foo.bar.net, logging in as user smith.
//
// The sample program assumes that both orgtable and dsttable are tables that
// exist in the database that connect-string (whether the default or one
// specified explicitly on the command line) connects to.
//
// The default origin table name is "orgevents"; the default destination table
// is "events".

namespace
{

class CreateTable : public Transactor
{
  string m_Table;

public:
  CreateTable(string Table) : Transactor("CreateTable"), m_Table(Table) {}

  void operator()(argument_type &T)
  {
    T.Exec("CREATE TABLE " + m_Table + "(year INTEGER, event TEXT)");
    cout << "Table " << m_Table << " created." << endl;
  }
};

class ClearTable : public Transactor
{
  string m_Table;

public:
  ClearTable(string Table) : Transactor("ClearTable"), m_Table(Table) {}

  void operator()(argument_type &T)
  {
    T.Exec("DELETE FROM " + m_Table);
  }

  void OnCommit()
  {
    cout << "Table successfully cleared." << endl;
  }
};


void CheckState(TableReader &R)
{
  if (!R != !bool(R))
    throw logic_error("TableReader " + R.Name() + " in inconsistent state!");
}


class CopyTable : public Transactor
{
  Transaction &m_orgTrans;  // Transaction giving us access to original table
  string m_orgTable;	    // Original table's name
  string m_dstTable;	    // Destination table's name

public:
  // Constructor--pass parameters for operation here
  CopyTable(Transaction &OrgTrans, string OrgTable, string DstTable) :
    Transactor("CopyTable"),
    m_orgTrans(OrgTrans),
    m_orgTable(OrgTable),
    m_dstTable(DstTable)
  {
  }

  // Transaction definition
  void operator()(argument_type &T)
  {
    TableReader Org(m_orgTrans, m_orgTable);
    TableWriter Dst(T, m_dstTable);

    CheckState(Org);

    // Copy table Org into table Dst.  This transfers all the data to the 
    // frontend and back to the backend.  Since in this example Ord and Dst are
    // really in the same database, we'd do this differently in real life; a
    // simple SQL query would suffice.
    Dst << Org;

    CheckState(Org);
  }

  void OnCommit()
  {
    cout << "Table successfully copied." << endl;
  }
};


}

int main(int argc, char *argv[])
{
  try
  {
    const char *ConnStr = argv[1];

    // Set up two connections to the backend: one to read our original table,
    // and another to write our copy
    LazyConnection orgC(ConnStr), dstC(ConnStr);

    // Select our original and destination table names
    const string orgTable = ((argc > 2) ? argv[2] : "orgevents");
    const string dstTable = ((argc > 3) ? argv[3] : "events");

    // Set up a transaction to access the original table from
    Transaction orgTrans(orgC, "test25org");
 
    // Attempt to create table.  Ignore errors, as they're probably one of:
    // (1) Table already exists--fine with us
    // (2) Something else is wrong--we'll just fail later on anyway
    try
    {
      dstC.Perform(CreateTable(dstTable));
    } 
    catch (const exception &)
    {
    }

    dstC.Perform(ClearTable(dstTable));
    dstC.Perform(CopyTable(orgTrans, orgTable, dstTable));
  }
  catch (const exception &e)
  {
    // All exceptions thrown by libpqxx are derived from std::exception
    cerr << "Exception: " << e.what() << endl;
    return 2;
  }
  catch (...)
  {
    // This is really unexpected (see above)
    cerr << "Unhandled exception" << endl;
    return 100;
  }

  return 0;
}

