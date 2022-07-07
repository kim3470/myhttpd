
const char * usage =
"                                                               \n"
"myhttpd server:                                                \n"
"                                                               \n"
"Usage: in one terminal window type:                            \n"
"                                                               \n"
"   myhttpd <port>                                              \n"
"                                                               \n"
"Where 1024 < port < 65536.             						\n"
"                                                               \n"
"Then in a browser window connect to:                           \n"
"   data.cs.purdue.edu:<port>                                   \n"
"                                                               \n"
"Authentication: the webpage is not accessible without confiming\n"
"username (mrmonke) and password (aaronkim).                    \n"
"                                                               \n"
"Include -f for process-based concurrency, -t for thread-based  \n"
"concurrency, -p for pool-of-thread based concurrency, or no    \n"
"flag for no concurrency (single-threaded).                     \n"
"                                                               \n";

#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <vector>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <stdio.h>
#include <limits.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>
#include <algorithm>
#include <tuple>
#include <dlfcn.h>

#define ASC 1
#define DESC 0
#define SORTNAME 0
#define SORTTIME 1
#define SORTSIZE 2

typedef void (*httprunfunc)(int ssock, const char* querystring);

using namespace std;

bool namecomp (tuple<string, string, string, string> const &t1, tuple<string, string, string, string> const &t2)
{
    return get<0>(t1) < get<0>(t2);
}
bool rnamecomp (tuple<string, string, string, string> const &t1, tuple<string, string, string, string> const &t2)
{
    return get<0>(t1) > get<0>(t2);
}
bool datecomp (tuple<string, string, string, string> const &t1, tuple<string, string, string, string> const &t2)
{
    if(get<1>(t1) == get<1>(t2))
    {
        return get<0>(t1) < get<0>(t2);
    }
    else
    {
        return get<1>(t1) < get<1>(t2);
    }
}
bool rdatecomp (tuple<string, string, string, string> const &t1, tuple<string, string, string, string> const &t2)
{
    if(get<1>(t1) == get<1>(t2))
    {
        return get<0>(t1) > get<0>(t2);
    }
    else
    {
        return get<1>(t1) > get<1>(t2);
    }
}
bool sizecomp (tuple<string, string, string, string> const &t1, tuple<string, string, string, string> const &t2)
{
    if(get<2>(t1) == get<2>(t2))
    {
        return get<0>(t1) < get<0>(t2);
    }
    else
    {
        return get<2>(t1) < get<2>(t2);
    }
}
bool rsizecomp (tuple<string, string, string, string> const &t1, tuple<string, string, string, string> const &t2)
{
    if(get<2>(t1) == get<2>(t2))
    {
        return get<0>(t1) > get<0>(t2);
    }
    else
    {
        return get<2>(t1) > get<2>(t2);
    }
}

int QueueLength = 5;
//cpp string for content type
string contype;
//cpp string for flag
string flag;
//int flag for ascending/descending (ascending by default)
int order;
//int flag for type of sorting (name by default)
int sorttype;
//really dumb info string for 302 requests
string info;
//field for ip address
string ip;
//clock for stats page
clock_t start, endtime;
//time used for stats page
double cpu_time_used;
//minimum request time
double minreqtime = 1000000;
//maximum request time
double maxreqtime = 0;
//fastest request doc
string fastest;
//slowest request doc
string slowest;
//for uptime
struct timespec before, after;
long uptime;
//for num requests
int numrequests = 0;

// Processes request
void processRequest( int socket );

//handle for sigchld zombie processes
void endzombie_c( int sig )
{
	pid_t pid = waitpid(-1, NULL, WSTOPPED);
	while(waitpid(-1, NULL, WNOHANG) > 0); 
	sig++;
}

void processRequestThread(int socket)
{
	processRequest(socket);
	close(socket);
} 

void poolSlave(int socket)
{
	while(1)
	{
		// Accept incoming connections
		struct sockaddr_in clientIPAddress;
		int alen = sizeof(clientIPAddress);
		int slaveSocket = accept(socket, (struct sockaddr *)&clientIPAddress, (socklen_t*)&alen);
		//set ip address
		ip = string(inet_ntoa(clientIPAddress.sin_addr));
		if (slaveSocket < 0) 
		{
			perror("accept");
			exit(-1);
		}
		//check if accept worked
		processRequest(slaveSocket);
		close(slaveSocket);
	} 	
}

int main( int argc, char ** argv )
{
	clock_gettime(CLOCK_REALTIME, &before);
	// Print usage if not enough arguments
	if ( argc < 2 ) 
	{
		fprintf( stderr, "%s", usage );
		exit( -1 );
	}
	//port comes from arguments
	int port;
	//2 args, assume single threaded
	if (argc == 2)
	{
		port = atoi( argv[1] );
		if(port <= 1024 || port >= 65536)
		{
			fprintf(stderr, "%s", usage);
			exit(-1);
		}
	}
	else if(argc == 3)
	{
		port = atoi(argv[2]);
		//set flag
		flag = string(argv[1]);
		if((flag != "-f" && flag != "-p" && flag != "-t") || (port <= 1024 || port >= 65536))
		{
			fprintf(stderr, "%s", usage);
			exit(-1);
		}
	}

	//handle interrupt for zombie processes
	struct sigaction sigchld_action;
	sigchld_action.sa_handler = endzombie_c;
	sigemptyset(&sigchld_action.sa_mask);
	sigchld_action.sa_flags = SA_RESTART;

	if(sigaction(SIGCHLD, &sigchld_action, NULL)){
		perror("sigaction");
		exit(0);
	}

	// Set the IP address and port for this server
	struct sockaddr_in serverIPAddress; 
	memset( &serverIPAddress, 0, sizeof(serverIPAddress) );
	serverIPAddress.sin_family = AF_INET;
	serverIPAddress.sin_addr.s_addr = INADDR_ANY;
	serverIPAddress.sin_port = htons((u_short) port);

	// Allocate a socket
	int masterSocket =  socket(PF_INET, SOCK_STREAM, 0);
	if ( masterSocket < 0) 
	{
		perror("socket");
		exit( -1 );
	}
	order = ASC;
	sorttype = SORTNAME;
	// Set socket options to reuse port. Otherwise we will
	// have to wait about 2 minutes before reusing the sae port number
	int optval = 1; 
	int err = setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR, (char *) &optval, sizeof( int ) );

	// Bind the socket to the IP address and port
	// this essentially "initializes" the port with the given socket ip address
	int error = bind( masterSocket,(struct sockaddr *)&serverIPAddress, sizeof(serverIPAddress) );
	if ( error ) 
	{
		perror("bind");
		exit( -1 );
	}

	// Put socket in listening mode and set the 
	// size of the queue of unprocessed connections
	error = listen( masterSocket, QueueLength);
	if (error) 
	{
		perror("listen");
		exit( -1 );
	}
	pid_t slave;
	//pool of threads based (-p)
	if(flag == "-p")
	{
		pthread_t tid[5];
		for(int i = 0; i < 5; i++)
		{
			pthread_create(&tid[i], NULL, (void * (*)(void *)) poolSlave, (void *) masterSocket);
		}
		poolSlave(masterSocket);
	}
	else
	{
		//continuously accept new requests
		while (1) 
		{
			// Accept incoming connections
			struct sockaddr_in clientIPAddress;
			int alen = sizeof(clientIPAddress);
			int slaveSocket = accept(masterSocket, (struct sockaddr *)&clientIPAddress, (socklen_t*)&alen);
			//set ip address
			ip = string(inet_ntoa(clientIPAddress.sin_addr));
			if (slaveSocket < 0) 
			{
				perror("accept");
				exit(-1);
			}

			//process based (-f)
			if(flag == "-f")
			{
				slave = fork(); //fork for processes (pulled from lab 5 slides)
				if(slave == 0)
				{
					// Process request (read/write to slave socket).
					processRequest(slaveSocket);

					shutdown(slaveSocket, SHUT_RDWR);
					// Close socket
					close(slaveSocket);
					exit(EXIT_SUCCESS);
				}
				close(slaveSocket); //also pulled from lab 5 slides
			}
			//thread based (-t)
			else if(flag == "-t")
			{
				//when thread ends recycle resources (pulled from slides)
				pthread_t thread;
				pthread_attr_t attr;
				pthread_attr_init(&attr);
				pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
				pthread_create(&thread, &attr, (void * (*)(void *)) processRequestThread, (void *) slaveSocket);
			}
			else
			{
				// Process request (read/write to slave socket).
				processRequest(slaveSocket);
				shutdown(slaveSocket, SHUT_RDWR);
				// Close socket
				close(slaveSocket);
			}
		}
	}
	
}

//write header (or error if necessary)
void makeHeader(int errcode, string &header)
{
	//response format:
	//HTTP/1.0 [200 Document follows] or [404 File Not Found] <crlf> 
	//Server: <Server-Type> <crlf> 
	//Content-type: <Document-Type> <crlf> 
	//<crlf> 
	//<Document Data> or <Error Message>
	//if errcode is 200 write "document follows"
	if(errcode == 200)
	{
		header += to_string(errcode) + string(" Document follows\r\n");
	}
	else if (errcode == 302)
	{
		//location header
		header += to_string(errcode) + string(" Found\r\n");
		//write server type (CS252 lab 5) and content type with crlfx2
		header += "Server: CS252 lab 5\r\n";
		header += string("Location: ") + info + string("\r\n\r\n");
		return;
	}
	//otherwise write error type
	else
	{
		if(errcode == 403)
		{
			header += to_string(errcode) + string(" Forbidden\r\n");
		}
		if(errcode == 404)
		{
			header += to_string(errcode) + string(" File Not Found\r\n");
		}
		
	}
	//write server type (CS252 lab 5) and content type with crlfx2
	header += "Server: CS252 lab 5\r\n";
	header += string("Content-type: ") + contype + string("\r\n\r\n");
	
}

void processRequest( int fd )
{
	//start the clock
	start = clock();
	//increment numrequests
	numrequests++;
	//logfile fp
	FILE * logfile = fopen("./http-root-dir/htdocs/logs", "a");
	// Buffer used to store the request received from the client
	string request;
	//string for the file type based on extension/if it's a directory
	string filetype;
	//buffer used to store the final content string
	string fcontent;
	//string for header
	string header = "HTTP/1.0 ";
	//string for tokens
	vector<string> tokens;
	vector<tuple<string, string, string, string>> entries;
	int n;

	// Current character being read
	unsigned char newChar;

	// prev character read
	unsigned char lastChar = 0;

	//sentinel boolean for tracking 2 <CR> in a row
	bool crlf = false;
	while ((n = read(fd, &newChar, sizeof(newChar))) > 0) 
	{
		//'\015' is carriage return, '\012' is line feed
		if (lastChar == '\015' && newChar == '\012') 
		{
			//if previous 2 chars were <CR> end loop
			if(crlf) 
			{
				break;
			}
			//if they werent <CR> set crlf sentinel to true
			else 
			{
				crlf = true;
			}
		}
		//otherwise if new 2 chars are not <CR> reset crlf sentinel
		else if (newChar != '\015')	
		{
			crlf = false;
		}
		//append new char to request buffer
		request += newChar;
		//set last character to current char
		lastChar = newChar;
	}
	
	//request is now in request string
	
	//make copy of request string
    string cpy = request;
	//replace \r and \n to be whitespace
	for(int i = 0; i < cpy.size(); i++)
	{
	    if(cpy[i] == '\r')
	    {
	        cpy[i] = 0;
	    }
	    if(cpy[i] == '\n')
	    {
	        cpy[i] = ' ';
	    }
	}
	//track position in string
	int pos;
	int count = 0;
	//token string
	string token;
	//tokenize just the request line by whitespace
	while((pos = cpy.find(' ')) != string::npos && count < 3)
	{
		//token is substring cpy from 0 to index 0 + pos (non-inclusive)
		token = cpy.substr(0, pos);
		tokens.push_back(token);
		cpy = cpy.substr(pos + 1);
		count++;
	}

	//tokens vector (just request):
	//index 0: request type (GET)
	//index 1: document requested
	//index 2: HTTP version (HTTP/1.0)
	//string for doc path
	string doc = tokens[1];
	string odoc = doc;
	//send request to logfile
	if(doc == "/")
	{
		odoc = "/index.html";
		fprintf(logfile, "%s %s\n", ip.c_str(), odoc.c_str());
	}
	else
	{
		fprintf(logfile, "%s %s\n", ip.c_str(), doc.c_str());
	}
	
	fclose(logfile);

	//in cpy, look for the authentication line. if it doesn't exist, prompt for auth
	if(cpy.find("Authorization: Basic bXJtb25rZTphYXJvbmtpbQ==") == string::npos)
	{
		//write corresponding response
		//string for authentication
		const char * passprompt = "HTTP/1.1 401 Unauthorized\r\n"
		                           "WWW-Authenticate: Basic realm=\"myhttpd-cs252\"\r\n\r\n";
		write(fd, passprompt, strlen(passprompt));
		return;
	}

	//string for first component
	string pref;
	//error code
	int err;
	// Send info to client (reply)
	char buf[PATH_MAX] = {0};
	string path = getcwd(buf, 256);
	path += "/http-root-dir/";
	//expand path with realpath
	string exp_path = realpath(path.c_str(), buf);
	//if expanded path doesn't contain http-root-dir
	if(exp_path.find("http-root-dir") == string::npos)
	{	
		//error 403, send error and return
		err = 403;
		//make header
		makeHeader(err, header);
		//write header
		write(fd, header.c_str(), header.size());
		//set fcontent
		fcontent = "Past http-root-dir";
		//write file contents
		write(fd, fcontent.c_str(), fcontent.size());
		//write newline
		write(fd, "\n", 1);
		return;
	}
	//if path contains cgi-bin: GET /cgi-bin/<script>?{<var>=<val>&}*{<var>=<val>} HTTP/1.1
	if(doc.find("/cgi-bin/") != string::npos)
	{
		header.clear();
		pid_t cgibin;
		string queries;
		
		//retrieve queries (if there are any)
		if(doc.find("?") == string::npos)
		{
			queries = string();
			//add the doc string
			exp_path += doc;
		}
		else
		{
			queries = doc.substr(doc.find("?") + 1, doc.size() - doc.find(' ', 2));
			exp_path += doc.substr(0, doc.find('?'));
		}
		//set env variables
		setenv("REQUEST_METHOD", "GET", 1);
		setenv("QUERY_STRING", queries.c_str(), 1);
		//custom header for cgi bin
		header = "HTTP/1.0 200 Document follows\r\nServer: CS252 lab 5\r\n";
		write(fd, header.c_str(), header.size());
		//if doc ends with ".so" -- loadable modules
		if(doc.find(".so") != string::npos)
		{
			void *lib = dlopen(exp_path.c_str(), RTLD_NOLOAD);
			if(lib == NULL)
			{
				lib = (void *) dlopen(exp_path.c_str(), RTLD_LAZY);
			}
			//clock stuff here
			//stat writing block
			endtime = clock();
			cpu_time_used = ((double) (endtime - start)) / CLOCKS_PER_SEC;
			if(cpu_time_used < minreqtime){minreqtime = cpu_time_used; fastest = odoc;}
			if(cpu_time_used > maxreqtime){maxreqtime = cpu_time_used; slowest = odoc;}
			clock_gettime(CLOCK_REALTIME, &after);
			uptime = after.tv_sec - before.tv_sec;
			//statfile fp
			FILE * statfile = fopen("./http-root-dir/htdocs/stats", "w");
			fprintf(statfile, "Lab 5 by: Aaron W. Kim\nServer uptime: %lds\nNumber of requests: %d\nMinimum request time: %s (%f)\nMaximum request time: %s (%f)\n", uptime, numrequests, fastest.c_str(), minreqtime, slowest.c_str(), maxreqtime);
			fclose(statfile);
			// Get function for httprun
			httprunfunc modrun;
			modrun = (httprunfunc) dlsym( lib, "httprun");
			modrun(fd, (char *)queries.c_str());
		}
		//if doc isnt'a module
		else
		{
			//dup of standard out
			int stout = dup(1);
			//set fd to stdout
			dup2(fd, 1);
			close(fd);
			//fork to cgibin ipid
			cgibin = fork();
			if(cgibin == 0)
			{
				//close stout fd
				close(stout);
				//set args and execv
				char * const args[] = {(char *) exp_path.c_str(), NULL};
				execv(args[0], args);
			}
			//restore out
			dup2(stout, 1);
			close(stout);
			//stat writing block
			endtime = clock();
			cpu_time_used = ((double) (endtime - start)) / CLOCKS_PER_SEC;
			if(cpu_time_used < minreqtime){minreqtime = cpu_time_used; fastest = odoc;}
			if(cpu_time_used > maxreqtime){maxreqtime = cpu_time_used; slowest = odoc;}
			clock_gettime(CLOCK_REALTIME, &after);
			uptime = after.tv_sec - before.tv_sec;
			//statfile fp
			FILE * statfile = fopen("./http-root-dir/htdocs/stats", "w");
			fprintf(statfile, "Lab 5 by: Aaron W. Kim\nServer uptime: %lds\nNumber of requests: %d\nMinimum request time: %s (%f)\nMaximum request time: %s (%f)\n", uptime, numrequests, fastest.c_str(), minreqtime, slowest.c_str(), maxreqtime);
			fclose(statfile);
		}
		return;
	}
	//if doc has a query ('?') but the prefix isn't cgi-mod
	if(doc.find('?') != string::npos)
	{
		//set err to 200 (we're writing the directory again)
		err = 200;
		filetype = "dir";
		//set the type flags 
		string sortrequest = doc.substr(doc.find('?') + 1);
		string reqtype = sortrequest.substr(sortrequest.find('=') + 1, 1);
		string ordertype = sortrequest.substr(sortrequest.find('=', 2) + 1, 1);
		if(reqtype == "N") {sorttype = SORTNAME;}
		else if(reqtype == "M") {sorttype = SORTTIME;}
		else if(reqtype == "S") {sorttype = SORTSIZE;}
		if(ordertype == "A") {order = ASC;}
		else if(ordertype == "D") {order = DESC;}
		//and then remove the request; this leaves just the directory
		doc = doc.substr(0, doc.find('?'));
	}
	//if doc is "/" (default), set to index.html
	if(doc == "/")
	{
		path += "htdocs/index.html";
		doc = "/index.html";
	}
	//if doc is not default
	else
	{
		//use stat to see type of file
		struct stat path_stat;	
		//complete the path
		//if there is no prefix, add htdocs. else, add the rest of the path.
		//set prefix
		pref = doc.substr(1, doc.find('/', 2) - 1);
		//if path + pref isn't a directory, set it to htdocs
		if (pref != "icons" && pref != "cgi-bin" && pref != "cgi-src") {path += "htdocs";}
		else {path.pop_back();}
		//add the rest of the path
		path += doc;
		stat(path.c_str(), &path_stat);	
		if(S_ISDIR(path_stat.st_mode)) {filetype = "dir";}
		else {filetype = "file";}

		//IF DOC IS A DIRECTORY (CHANGE TO IF STAT REVEALS A DIR)
		if(filetype == "dir")
		{
			err = 200;
			//handle case with no slash
			if(doc.rfind('/') != doc.size() - 1)
			{
				err = 302;
				info = doc + "/";
				makeHeader(err, header);
				//write header
				write(fd, header.c_str(), header.size());
			}
			
			filetype = "dir";
			contype = "text/html";
		}
		
	}
	//set filetype if not a directory or sort request
	if(filetype != "dir")
	{	
		//if doc has no extension but is a file
		if(doc.rfind('.') == string::npos)
		{
			//set filetype to exe
			filetype = ".exe";
		}
		else 
		{
			//set extension by using rfind
			filetype = doc.substr(doc.rfind('.'));
		}
		//determine content type with big if/else
		if(filetype == ".html" || filetype == ".html/") {contype = "text/html";}
		else if (filetype == ".gif" || filetype == ".gif/") {contype = "image/gif";}
		else if (filetype == ".svg" || filetype == ".svg/") {contype = "image/svg+xml";}
		else if (filetype == ".jpg" || filetype == ".jpg/" || filetype == ".jpeg" || filetype == ".jpeg/") {contype = "image/jpeg";}
		else if (filetype == ".png" || filetype == ".png/") {contype = "image/png";}
		else {contype = "text/plain";}
	}
	//used for file statistics 
	struct stat sb{};
	//file pointer for reading non-directory files
	FILE * ffile;
	//dir pointer for reading directory
	DIR * fdir;
	//if the file is a directory
	if(filetype == "dir")
	{
		//try opening directory
		fdir = opendir(path.c_str());
		//if doesn't exist, error is 404 and return
		if(fdir == NULL)
		{
			err = 404;
			//set fcontent to perror message
			fcontent = string(strerror(errno));
			//make header
			makeHeader(err, header);
			//write header
			write(fd, header.c_str(), header.size());
			//write file contents
			write(fd, fcontent.c_str(), fcontent.size());
			//write newline
			write(fd, "\n", 1);
			return;
		}
	}
	//if file is not a directory or a sort request
	else if (filetype != "dir")
	{
		//set ffile
		ffile = fopen(path.c_str(), "r");
		//if ffile doesn't exist return a 404
		if (ffile == NULL)
		{
			err = 404;
			//set fcontent to perror message
			fcontent = string(strerror(errno));
			//make header
			makeHeader(err, header);
			//write header
			write(fd, header.c_str(), header.size());
			//write file contents
			write(fd, fcontent.c_str(), fcontent.size());
			//write newline
			write(fd, "\n", 1);
			return;
		}
		//otherwise set err to 200
		else
		{
			err = 200;
		}
	}
	//make header string
	makeHeader(err, header);
	//if error code is 200 (valid file) and file is not a directory read fcontent string
	if(err == 200 && filetype != "dir")
	{
		//set fcontent string
		stat(path.c_str(), &sb);
		fcontent.resize(sb.st_size);
		fread(const_cast<char*>(fcontent.data()), sb.st_size, 1, ffile);
		fclose(ffile);
		//write header
		write(fd, header.c_str(), header.size());
		//write file content string
		write(fd, fcontent.c_str(), fcontent.size());
		//stat writing block
		endtime = clock();
	    cpu_time_used = ((double) (endtime - start)) / CLOCKS_PER_SEC;
		if(cpu_time_used < minreqtime){minreqtime = cpu_time_used; fastest = odoc;}
		if(cpu_time_used > maxreqtime){maxreqtime = cpu_time_used; slowest = odoc;}
		clock_gettime(CLOCK_REALTIME, &after);
		uptime = after.tv_sec - before.tv_sec;
		//statfile fp
		FILE * statfile = fopen("./http-root-dir/htdocs/stats", "w");
		fprintf(statfile, "Lab 5 by: Aaron W. Kim\nServer uptime: %lds\nNumber of requests: %d\nMinimum request time: %s (%f)\nMaximum request time: %s (%f)\n", uptime, numrequests, fastest.c_str(), minreqtime, slowest.c_str(), maxreqtime);
		fclose(statfile);
	}
	//if error code is 200 and file is a directory write html into fcontent
	else if(err == 200 && filetype == "dir")
	{
		string parent = path.substr(path.find(doc));
		//remove last slash
		parent.pop_back();
		parent = parent.substr(0, parent.rfind('/') + 1);
		//write header
		write(fd, header.c_str(), header.size());
		//strings for the 3 distinct hrefs
		string namehref, datehref, sizehref;
		namehref = "C=N;O=";
		datehref = "C=M;O=";
		sizehref = "C=S;O=";
		//set href orders
		if(sorttype == SORTNAME)
		{
			if(order == ASC)
			{
				namehref += "D";
			}
			else
			{
				namehref += "A";
			}
			datehref += "A";
			sizehref += "A";
		}
		if(sorttype == SORTTIME)
		{
			if(order == ASC)
			{
				datehref += "D";
			}
			else
			{
				datehref += "A";
			}
			namehref += "A";
			sizehref += "A";
		}
		if(sorttype == SORTSIZE)
		{
			if(order == ASC)
			{
				sizehref += "D";
			}
			else
			{
				sizehref += "A";
			}
			namehref += "A";
			datehref += "A";
		}
		//write starting of page in html and header
		dprintf(fd, "<html>");
		dprintf(fd, "<body>");
		dprintf(fd, "<h1>Index of %s<h1>", path.c_str());
		dprintf(fd, "<table>");
		dprintf(fd, "<tbody>");
		//first row (name, date modified, size, description)
		dprintf(fd, "<tr><th valign=\"top\"><img src=\"/icons/blank.gif\" alt=\"[ICO]\"></th><th><a href=\"?%s\">Name</a></th><th><a href=\"?%s\">Last modified</a></th><th><a href=\"?%s\">Size</a></th><th><a href=\"?%s\">Description</a></th></tr>", namehref.c_str(), datehref.c_str(), sizehref.c_str(), namehref.c_str());
		//print the first line
		dprintf(fd, "<tr><th colspan=\"5\"><hr></th></tr>");
		//print the parent directory button
		dprintf(fd, "<tr><td valign=\"top\"><img src=\"/icons/back.gif\" alt=\"[PARENTDIR]\"></td><td><a href=\"%s\">Parent Directory</a></td><td>&nbsp;</td><td align=\"right\">  - </td><td>&nbsp;</td></tr>", parent.c_str());
		//stat struct for entry data
		struct stat entstat;
		//strings for name, date, size, and type for icon
		string name, date, size, type, html;
		//open directory path
		DIR * d = opendir(path.c_str());
		if (d == NULL)
		{
			cerr << "dir doesn't exist" << endl;
			exit(1);
		}
		//iterate through directory entries
		for (dirent * ent = readdir(d); NULL != ent; ent = readdir(d))
		{
			//set temporary path (path to entry)
			string temppath = path + string(ent->d_name);
			//stat struct on temppath
			stat(temppath.c_str(), &entstat);
			//if entry name doesn't start with "." (hidden file)
			if(string(ent->d_name).find('.') != 0)
			{
				if(S_ISDIR(entstat.st_mode))
				{
					filetype = "dir";
				}
				else if(temppath.rfind('.') == string::npos)
				{
					filetype = "unknown";
				}
				else
				{
					filetype = temppath.substr(temppath.rfind('.'));
				}
				//set size (nonregular files have size set to "-")
				if(S_ISREG(entstat.st_mode))
				{
					//get filesize in bytes
					size = to_string((int)entstat.st_size);
				}
				else
				{
					size = "-";
				}
				//set date modified
				char timbuf[200];
				struct tm *tm;
				tm = localtime(&entstat.st_mtime);
				sprintf(timbuf, "%d-%d-%d %d:%d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min);
				date = string(timbuf);
				//set name
				name = string(ent->d_name);
				//set type
				if(filetype == ".html" || filetype == ".html/" || filetype == ".cc" || filetype == ".cc/" || filetype == ".c" || filetype == ".c/"  || 
						filetype == ".txt"  || filetype == ".txt/") {type = "/icons/text.gif";}
				else if (filetype == ".gif" || filetype == ".gif/" || filetype == ".svg" || filetype == ".svg/" || filetype == ".jpg" || 
						filetype == ".jpg/" || type == ".jpeg" || filetype == ".jpeg/" || filetype == ".png" || filetype == ".png/") {type = "/icons/image.gif";}
				else if (filetype == "dir") {type = "/icons/folder.gif"; name += "/";}
				else {type = "/icons/unknown.gif";}
				//set html based on type
				html = "<tr><td valign=\"top\"><img src=\"" + type + "\" alt=\"[IMG]\"></td><td><a href=\"" + name + "\">" + name + "</a></td><td align=\"right\">" + date + "</td><td align=\"right\">" + size + "</td><td>&nbsp;</td></tr>";
				entries.push_back(make_tuple(name, date, size, html));
			}
		}
		//sort the vector according to the column and order
		if(sorttype == SORTNAME && order == ASC)
		{
			sort(entries.begin(), entries.end(), &namecomp);
		}
		else if(sorttype == SORTNAME && order == DESC)
		{
			sort(entries.begin(), entries.end(), &rnamecomp);
		}
		else if(sorttype == SORTTIME && order == ASC)
		{
			sort(entries.begin(), entries.end(), &datecomp);
		}
		else if(sorttype == SORTTIME && order == DESC)
		{
			sort(entries.begin(), entries.end(), &rdatecomp);
		}
		else if(sorttype == SORTSIZE && order == ASC)
		{
			sort(entries.begin(), entries.end(), &sizecomp);
		}
		else if(sorttype == SORTSIZE && order == DESC)
		{
			sort(entries.begin(), entries.end(), &rsizecomp);
		}
		//output html
		const char * htmlstring;
		for(auto ent : entries)
		{
			htmlstring = get<3>(ent).c_str();
			dprintf(fd, htmlstring);
		}

		//print the last line
		dprintf(fd, "<tr><th colspan=\"5\"><hr></th></tr>");
		//end all tags
		dprintf(fd, "</tbody>");
		dprintf(fd, "</table>");
		dprintf(fd, "</body>");
		dprintf(fd, "</html>");

		//stat writing block
		endtime = clock();
	    cpu_time_used = ((double) (endtime - start)) / CLOCKS_PER_SEC;
		if(cpu_time_used < minreqtime){minreqtime = cpu_time_used; fastest = odoc;}
		if(cpu_time_used > maxreqtime){maxreqtime = cpu_time_used; slowest = odoc;}
		clock_gettime(CLOCK_REALTIME, &after);
		uptime = after.tv_sec - before.tv_sec;
		//statfile fp
		FILE * statfile = fopen("./http-root-dir/htdocs/stats", "w");
		fprintf(statfile, "Lab 5 by: Aaron W. Kim\nServer uptime: %lds\nNumber of requests: %d\nMinimum request time: %s (%f)\nMaximum request time: %s (%f)\n", uptime, numrequests, fastest.c_str(), minreqtime, slowest.c_str(), maxreqtime);
		fclose(statfile);
	}
}
