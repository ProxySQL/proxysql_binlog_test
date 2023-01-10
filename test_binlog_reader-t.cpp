#include <iostream>
#include <thread>
#include <string>
#include <atomic>

#include "Slave.h"
#include "DefaultExtState.h"

const uint32_t NUM_ROWS = 3000;
std::atomic_bool stop(false);
std::atomic_bool ok(true);
slave::Slave* sl = NULL;

void callback(const slave::RecordSet& event) {
	if (
		event.db_name != "binlog_db" ||
		event.tbl_name != "gtid_test" ||
		event.type_event != slave::RecordSet::Write
	)
		ok = false;
}

bool isStopping() {
	return stop;
}

std::string gtid_executed_to_string(slave::Position &curpos) {
	std::string gtid_set { "" };

	for (auto it=curpos.gtid_executed.begin(); it!=curpos.gtid_executed.end(); ++it) {
		std::string s = it->first;
		s.insert(8,"-");
		s.insert(13,"-");
		s.insert(18,"-");
		s.insert(23,"-");
		s = s + ":";

		for (auto itr = it->second.begin(); itr != it->second.end(); ++itr) {
			std::string s2 = s;
			s2 = s2 + std::to_string(itr->first);
			s2 = s2 + "-";
			s2 = s2 + std::to_string(itr->second);
			s2 = s2 + ",";
			gtid_set = gtid_set + s2;
		}
	}

	if (gtid_set.empty() == false)
		gtid_set.pop_back();

	return gtid_set;
}

void dummy_query(MYSQL &mysql) {
	// This query do not disable multiplexing
	if (mysql_query(&mysql, "SELECT 1"))
		fprintf(stderr, "%s\n", mysql_error(&mysql));
	MYSQL_RES *result = mysql_store_result(&mysql);
	mysql_free_result(result);
}

void disable_multiplexing_query(MYSQL &mysql) {
	if (mysql_query(&mysql, "SELECT @@hostname"))
		fprintf(stderr, "%s\n", mysql_error(&mysql));
	MYSQL_RES *result = mysql_store_result(&mysql);
	mysql_free_result(result);
}

int create_testing_tables(MYSQL* mysql_server) {
	// Create the testing database
	mysql_query(mysql_server, "CREATE DATABASE IF NOT EXISTS binlog_db");
	mysql_query(mysql_server, "DROP TABLE IF EXISTS binlog_db.gtid_test");

	if (mysql_query(
		mysql_server,
		"CREATE TABLE IF NOT EXISTS binlog_db.gtid_test ("
		"  id INTEGER NOT NULL AUTO_INCREMENT,"
		"  a INT NOT NULL,"
		"  c varchar(255),"
		"  pad CHAR(60),"
		"  PRIMARY KEY (id)"
		")"
	))
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}

std::string random_string( size_t length )
{
    auto randchar = []() -> char
    {
        const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
        const size_t max_index = (sizeof(charset) - 1);
        return charset[ rand() % max_index ];
    };
    std::string str(length,0);
    std::generate_n( str.begin(), length, randchar );
    return str;
}

template<typename ... Args>
std::string string_format( const std::string& format, Args ... args )
{
    int size_s = std::snprintf( nullptr, 0, format.c_str(), args ... ) + 1; // Extra space for '\0'
    if( size_s <= 0 ){ throw std::runtime_error( "Error during formatting." ); }
    auto size = static_cast<size_t>( size_s );
    std::unique_ptr<char[]> buf( new char[ size ] );
    std::snprintf( buf.get(), size, format.c_str(), args ... );
    return std::string( buf.get(), buf.get() + size - 1 ); // We don't want the '\0' inside
}

int insert_random_data(MYSQL* proxysql_mysql, uint32_t rows) {
	int rnd_a = rand() % 1000;
	std::string rnd_c = random_string(rand() % 100 + 5);
	std::string rnd_pad = random_string(rand() % 50 + 5);

	for (uint32_t i = 0; i < rows; i++) {
		std::string update_query =
			string_format(
				"INSERT INTO binlog_db.gtid_test (a, c, pad) VALUES ('%d', '%s', '%s')",
				i, rnd_c.c_str(), rnd_pad.c_str()
			);
		mysql_query(proxysql_mysql, update_query.c_str());
	}

	return EXIT_SUCCESS;
}

void binlog_reader(const std::function<void(MYSQL &mysql)> &send_query) {
	slave::MasterInfo masterinfo;

	masterinfo.conn_options.mysql_host = "127.0.0.1";
	masterinfo.conn_options.mysql_port = 6033;
	masterinfo.conn_options.mysql_user = "root";
	masterinfo.conn_options.mysql_pass = "root";

	try {
		slave::DefaultExtState sDefExtState;
		slave::Slave slave(masterinfo, sDefExtState);
		sl = &slave;

		slave.setCallback("binlog_db", "gtid_test", callback);
		slave.init();
		slave.enableGtid();

		slave::Position curpos = slave.getLastBinlogPos();
		std::string s1 = gtid_executed_to_string(curpos);

		// Wait until a valid 'GTID' has been executed for requesting binlog
		while (s1.empty() && !isStopping()) {
			usleep(1000 * 1000);

			curpos = slave.getLastBinlogPos();
			s1 = gtid_executed_to_string(curpos);
		}

		sDefExtState.setMasterPosition(curpos);

		slave.createDatabaseStructure();

		try {
			slave.get_remote_binlog(isStopping, dummy_query);
		} catch (std::exception& ex) {
			std::cout << "Error reading: " << ex.what() << std::endl;
		}

	} catch (std::exception& ex) {
		std::cout << "Error initializing: " << ex.what() << std::endl;
	}
}

int main(int argc, char** argv) {
	MYSQL* proxysql_mysql = mysql_init(NULL);

	if (!mysql_real_connect(proxysql_mysql, "127.0.0.1", "root", "root", NULL, 6033, NULL, 0)) {
		fprintf(stderr, "File %s, line %d, Error: %s\n", __FILE__, __LINE__, mysql_error(proxysql_mysql));
		return EXIT_FAILURE;
	}

	int rc = create_testing_tables(proxysql_mysql);
	if (rc != EXIT_SUCCESS) {
		mysql_close(proxysql_mysql);
		return EXIT_FAILURE;
	}

	std::thread binlog_with_multiplexing(binlog_reader, std::cref(dummy_query));

	rc = insert_random_data(proxysql_mysql, NUM_ROWS);
	if (rc != EXIT_SUCCESS) {
		mysql_close(proxysql_mysql);
		stop = true;
		sl->close_connection();
		binlog_with_multiplexing.join();
		return EXIT_FAILURE;
	}

	stop = true;
	sl->close_connection();
	binlog_with_multiplexing.join();

	stop = false;
	sl = NULL;

	std::thread binlog_without_multiplexing(binlog_reader, std::cref(disable_multiplexing_query));

	rc = insert_random_data(proxysql_mysql, NUM_ROWS);
	if (rc != EXIT_SUCCESS) {
		mysql_close(proxysql_mysql);
		stop = true;
		sl->close_connection();
		binlog_without_multiplexing.join();
		return EXIT_FAILURE;
	}

	stop = true;
	sl->close_connection();
	binlog_without_multiplexing.join();

	stop = false;
	sl = NULL;

	mysql_close(proxysql_mysql);

	if (!ok)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
