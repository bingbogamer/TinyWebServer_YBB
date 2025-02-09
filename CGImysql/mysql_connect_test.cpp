#include <iostream>
#include <mysql/mysql.h>
#include <string>
 
#include <assert.h>
 
int main()
{
    MYSQL *ms_conn = mysql_init(NULL);
    if (ms_conn == NULL)
    {
        std::cout << "Error: mysql_init failed." << std::endl;
        return 0;
    }
    std::cout << "Info: mysql_init successful." << std::endl;
 
    MYSQL *ms_res = NULL;
    ms_res = mysql_real_connect(ms_conn, "localhost", "root", "rootpass", "tinywebserver_ybb", 3306, NULL, CLIENT_FOUND_ROWS);
    // mysql_real_connect(connSql, "localhost", "root", "0804", "tinywebserver_ybb", 3306, NULL, 0);

    if (ms_res == NULL)
    {
        std::cout << "Error: connect mysql failed: " << mysql_error(ms_conn) << std::endl;
        mysql_close(ms_conn), ms_conn = NULL;
        return 0;
    }
    std::cout << "Info: mysql connect successful." << std::endl;
	
    // ... // 其他操作
 
    // 使用完释放系统资源
    mysql_close(ms_conn), ms_conn = NULL;
    return 0;
}