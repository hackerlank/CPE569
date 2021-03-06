#include "socket.h"

#ifdef WIN32
#include <winsock.h>
#define ioctl ioctlsocket
int (__stdcall*close)(unsigned) = closesocket;
typedef int ACCEPT_TYPE;
const int MSG_NOSIGNAL = 0;
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <sys/ioctl.h>
typedef int SOCKET;
const int SOCKET_ERROR = -1;
const int INVALID_SOCKET = -1;
typedef unsigned ACCEPT_TYPE;
#endif

#include <iostream>
#include <map>
#include <string>
#include <boost/iostreams/stream.hpp>

using namespace std;

#define SD_SEND 1

/********************************************/
/********* Global Data for sockets **********/
/********************************************/
namespace {
#ifdef WIN32
   WSAData wsaData;
   map<int, const char*> errors;
#endif

#ifdef SOCK_EXCEPTION
   void sockError(const char *err)
   {
      // throw an exception here...
   }
#else
   void sockError(const char *err)
   {
      cerr << "Socket error: " << err << endl;
      //exit(-1);
   }
#endif

   void sockError()
   {
#ifdef WIN32
      int e = WSAGetLastError();
      if (e == WSANOTINITIALISED)
         sockError("WSAStartup not yet called");
      else
         sockError(errors[e]);
#else
      perror("Socket");
#endif
   }

   u_long getAddress(const char* host);
   void setupErrors();
   void shutdownSocket(SOCKET sd);

   int sockListen(SOCKET sd, int backlog)
   {
      return listen(sd, backlog);
   }

   SOCKET sockAccept(SOCKET sd, sockaddr *addr, ACCEPT_TYPE *size)
   {
      return accept(sd, addr, size);
   }

   int sockSend(SOCKET sd, const char *buf, int len, int flags)
   {
      return send(sd, buf, len, flags);
   }
   
   int sockRecv(SOCKET sd, char *buf, int len, int flags)
   {
      return recv(sd, buf, len, flags);
   }

   int sockSelect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, timeval *timeout)
   {
      return select(nfds, readfds, writefds, exceptfds, timeout);
   }
}


/********************************************/
/************* Socket Namespace *************/
/********************************************/
namespace sock {

void setupSockets()
{
#ifdef WIN32
   if (WSAStartup(MAKEWORD(2,0), &wsaData) != 0)
      sockError();

   setupErrors();
#endif
}

void shutdownSockets()
{
#ifdef WIN32
   WSACleanup();
#endif
}

int bytesRead = 0;
int bytesSent = 0;

int getBytesSent()
{
   int temp = bytesSent;
   bytesSent = 0;
   return temp;
}

int getBytesRead()
{
   int temp = bytesRead;
   bytesRead = 0;
   return temp;
}

bool Socket::select(int ms)
{
   timeval timeout;
   fd_set set;
   int ret;
   u_long sd = this->getSocket();

   FD_ZERO(&set);
   FD_SET(sd, &set);

   if (ms >= 0) {
      timeout.tv_sec = ms / 1000;
      timeout.tv_usec = (ms % 1000) * 1000;
      ret = sockSelect(sd+1, &set, 0, 0, &timeout);
   } else {
      ret = sockSelect(sd+1, &set, 0, 0, 0);
   }

   if (ret == SOCKET_ERROR)
      setError();

   return ret != 0;
}


/************* Connection class *************/
struct ConnectionInfo {
   SOCKET sd;
   u_long addr;
   u_short port;
   bool active, mreads;
   ~ConnectionInfo() { if (active) shutdownSocket(sd); active = false; }
};

Connection::Connection(const char *host, int port)
{
   info.reset(new ConnectionInfo);

   info->active = false;
   info->addr = getAddress(host);
   info->port = htons(port);
   if (info->addr == INADDR_NONE) {
      sockError((string("Could not resolve host name: ") + string(host)).c_str());
      return;
   }

   info->sd = socket(AF_INET, SOCK_STREAM, 0);
   if (info->sd == INVALID_SOCKET) {
      sockError();
      return;
   }
   
#ifdef WIN32
   char yes = 1;
   if (setsockopt(info->sd, IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(char)) == -1) {
#else
   int yes = 1;
   if (setsockopt(info->sd, IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(int)) == -1) {
#endif
      sockError();
      return;
   }

   sockaddr_in sinRemote;
   sinRemote.sin_family = AF_INET;
   sinRemote.sin_addr.s_addr = info->addr;
   sinRemote.sin_port = info->port;
   if (connect(info->sd, (sockaddr*)&sinRemote, sizeof(sockaddr_in)) == SOCKET_ERROR) {
      sockError();
      return;
   }

   info->active = true;
   info->mreads = false;
}

Connection::Connection(unsigned long host, int port)
{
   info.reset(new ConnectionInfo);

   info->active = false;
   info->addr = host;
   info->port = htons(port);

   info->sd = socket(AF_INET, SOCK_STREAM, 0);
   if (info->sd == INVALID_SOCKET) {
      sockError();
      return;
   }
   
#ifdef WIN32
   char yes = 1;
   if (setsockopt(info->sd, IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(char)) == -1) {
#else
   int yes = 1;
   if (setsockopt(info->sd, IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(int)) == -1) {
#endif
      sockError();
      return;
   }

   sockaddr_in sinRemote;
   sinRemote.sin_family = AF_INET;
   sinRemote.sin_addr.s_addr = info->addr;
   sinRemote.sin_port = info->port;
   if (connect(info->sd, (sockaddr*)&sinRemote, sizeof(sockaddr_in)) == SOCKET_ERROR) {
      sockError();
      return;
   }

   info->active = true;
   info->mreads = false;
}

Connection::Connection(ConnectionInfo *info) : info(info)
{
   // intentionally empty
   info->mreads = false;
}

bool Connection::send(const Packet &p)
{
   unsigned int bytesSent = 0, sendCount = 0, sendVal;
   sock::bytesSent += p.size();

   if (!info->active)
      return false;

   while (info->active && bytesSent < p.size()) {
      sendCount++;
      sendVal = sockSend(info->sd, (const char *)&p[bytesSent], p.size()-bytesSent, MSG_NOSIGNAL);
      bytesSent += sendVal;
      if (sendVal == SOCKET_ERROR) {
         cerr << "-- send failed on attempt number " << sendCount << "." << endl;
         setError();
         return false;
      } else if (sendVal == 0) {
         cerr << "-- closing connection (sent 0 bytes)" << endl;
         return info->active = false;
      } else if (bytesSent != p.size()) {
         cerr << "-- sent " << bytesSent << " out of " << p.size() << endl;
      }
      
   }
   
   if (sendCount > 1)
      cerr << "-- send took " << sendCount << " attempts to send packet." << endl;

   return true;
}

bool Connection::recv(Packet &p, int size)
{
   int bytesRead = 0, readCount = 0;
   sock::bytesRead += size;

   if (!info->active)
      return false;

   if (size < 0) {
      select();
      size = available();
      if (size == 0) {
         cerr << "remote end hung up while waiting for recv" << endl;
         info->active = false;
         return false;
      }
   }

   p.reset(size);

   while (info->active && bytesRead < size) {
      readCount++;
      int readSize = sockRecv(info->sd, (char *)&p[bytesRead], size-bytesRead, 0);
      bytesRead += readSize;
      if (readSize == SOCKET_ERROR) {
         cerr << "-- recv failed on attempt number " << readCount << "." << endl;
         setError();
         return false;
      } else if (readSize == 0) {
         cerr << "-- closing connection (read 0 bytes)" << endl;
         return info->active = false;
      } else if (readSize == -1) {
         cerr << "-- got -1 in recv" << endl;
         setError();
         return false;
      } else if (bytesRead != size) {
         cerr << "-- read " << bytesRead << " out of " << size << endl;
      }
   }

   if (readCount > 1) {
      cerr << "-- recv took " << readCount << " attempts to retrieve packet." << endl;
      info->mreads = true;
   } else {
      info->mreads = false;
   }

   return true;
}

bool Connection::tookMultipleReads()
{
   return info->mreads;  
}

void Connection::close()
{
   if (info->active)
      shutdownSocket(info->sd);
   info->active = false;
}

void Connection::setError()
{
#ifdef WIN32
   int err = WSAGetLastError();
   if (err == WSAECONNRESET || err == WSAECONNABORTED) {
      cerr << "-- " << errors[WSAGetLastError()] << endl;
   } else {
      sockError();
   }
#else
   perror("Connection::error");
#endif
   info->active = false;
}

Connection::operator bool() const
{
   return info->active;
}

unsigned long Connection::available()
{
   u_long size = 0;

   if (ioctl(info->sd, FIONREAD, &size))
      setError();

   return size;
}

unsigned long Connection::getSocket() const
{
   return info->sd;
}

bool Connection::check()
{
   return info->active = !(info->active && select() && available() == 0);
}


unsigned long Connection::getAddr()
{
   return info->addr;
}

int Connection::getPort()
{
   return ntohs(info->port);
}


/************* Server class *************/
struct ServerInfo {
   SOCKET sd;
   u_long addr;
   u_short port;
   bool active;
   //~ServerInfo() { nobody cares about closing server sockets? }
};

Server::Server(int port, const char *addr)
{
   info.reset(new ServerInfo);

   info->active = false;
   info->addr = getAddress(addr);
   info->port = htons((short)port);

   if (info->addr == INADDR_NONE) {
      sockError((string("Bad interface: ") + string(addr)).c_str());
      return;
   }

   info->sd = socket(AF_INET, SOCK_STREAM, 0);
   if (info->sd == INVALID_SOCKET) {
      sockError();
      return;
   }

   sockaddr_in sinRemote;
   sinRemote.sin_family = AF_INET;
   sinRemote.sin_addr.s_addr = info->addr;
   sinRemote.sin_port = info->port;
   if (bind(info->sd, (sockaddr*)&sinRemote, sizeof(sockaddr_in)) == SOCKET_ERROR) {
      sockError();
      return;  
   }
   
#ifdef WIN32
   char yes = 1;
   if (setsockopt(info->sd, SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(char)) == -1) {
#else
   int yes = 1;
   if (setsockopt(info->sd, SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int)) == -1) {
#endif
      sockError();
      return;
   }
   

#ifdef WIN32
   yes = 1;
   if (setsockopt(info->sd, IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(char)) == -1) {
#else
   yes = 1;
   if (setsockopt(info->sd, IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(int)) == -1) {
#endif
      sockError();
      return;
   }

   info->port = ntohs(sinRemote.sin_port);
   info->active = true;
}

void Server::listen(int queueSize)
{
   if (sockListen(info->sd, queueSize) == SOCKET_ERROR) {
      sockError();
      info->active = false;
   }
}

Connection Server::accept()
{
   ConnectionInfo *conn = new ConnectionInfo;
   ACCEPT_TYPE size = sizeof(sockaddr_in);
   sockaddr_in addr;
   conn->sd = sockAccept(info->sd, (sockaddr*)&addr, &size);
   if (conn->sd == INVALID_SOCKET) {
      delete conn;
      sockError();
      info->active = false;
   }

   conn->addr = addr.sin_addr.s_addr;
   conn->port = addr.sin_port;
   conn->active = true;

   return Connection(conn);
}

int Server::port()
{
   return info->port;
}

void Server::close()
{
   if (info->active)
      shutdownSocket(info->sd);
   info->active = false;
}

unsigned long Server::getSocket() const
{
   return info->sd;
}

bool Server::check()
{
   return info->active;
}

Server::operator bool() const
{
   return info->active;
}

void Server::setError()
{
#ifdef WIN32
   int err = WSAGetLastError();
   if (err == WSAECONNRESET || err == WSAECONNABORTED) {
      info->active = false;
      cerr << "--Server: " << errors[WSAGetLastError()] << endl;
   } else
      sockError();
#else
   info->active = false;
   perror("Server::erorr");
#endif
}



/************* Packet class *************/
void Packet::checkSpace(int amount)
{
   if (bitCursor != 0) {
      cursor++;
      bitCursor = 0;
   }
   amount = cursor - size() + amount;
   if (amount > 0)
      insert(end(), amount, 0);
}

Packet &Packet::writeBit(bool b)
{
   if ((bitCursor & 7) == 0)
      checkSpace(1);

   at(cursor) &= (b? 1 : 0) << bitCursor++;

   return *this;
}

Packet &Packet::writeByte(unsigned char b)
{
   checkSpace(1);
   at(cursor++) = b;
   return *this;
}

Packet &Packet::writeShort(unsigned short s)
{
   u_short sh = htons(s);
   u_char *p = (u_char*)&sh;

   checkSpace(2);
   at(cursor++) = *(p++);
   at(cursor++) = *(p++);

   return *this;
}

Packet &Packet::writeInt(int i)
{
   u_int in = htonl(i);
   u_char *p = (u_char*)&in;

   checkSpace(4);
   at(cursor++) = *(p++);
   at(cursor++) = *(p++);
   at(cursor++) = *(p++);
   at(cursor++) = *(p++);

   return *this;
}

Packet &Packet::writeUInt(unsigned i)
{
   return writeInt(i);
}

Packet &Packet::writeLong(unsigned long l)
{
   u_long in = htonl(l);
   u_char *p = (u_char*)&in;

   checkSpace(4);
   at(cursor++) = *(p++);
   at(cursor++) = *(p++);
   at(cursor++) = *(p++);
   at(cursor++) = *(p++);

   return *this;
}

Packet &Packet::writeFloat(float f)
{
   u_char *p = (u_char*)&f;

   checkSpace(4);
   at(cursor++) = *(p++);
   at(cursor++) = *(p++);
   at(cursor++) = *(p++);
   at(cursor++) = *(p++);

   return *this;
}

Packet &Packet::writeCStr(const char *str)
{
   checkSpace(strlen(str)+1);
   unsigned char temp = 1;
   unsigned char *ptr = (unsigned char *)str;

   do {
      temp = *ptr++;
      at(cursor++) = temp;
   } while (temp != 0);

   return *this;
}

Packet &Packet::writeStdStr(const std::string &str)
{
   writeCStr(str.c_str());
   return *this;
}

Packet &Packet::writeData(const unsigned char *data, int length)
{
   checkSpace(length);
   for (int i = 0; i < length; i++)
      at(cursor++) = *(data++);

   return *this;
}

Packet &Packet::readBit(bool &b)
{
   if (bitCursor == 7) {
      cursor++;
      bitCursor = 0;
   }

   b = (at(cursor) & 1 << bitCursor++) != 0;

   return *this;
}

Packet &Packet::readByte(unsigned char &b)
{
   b = at(cursor++);
   bitCursor = 0;
   return *this;
}

Packet &Packet::readShort(unsigned short &s)
{
   u_char *p = (u_char*)&s;
   
   *p++ = at(cursor++);
   *p++ = at(cursor++);
   s = ntohs(s);

   bitCursor = 0;
   return *this;
}

Packet &Packet::readInt(int &i)
{
   u_char *p = (u_char*)&i;
   
   *p++ = at(cursor++);
   *p++ = at(cursor++);
   *p++ = at(cursor++);
   *p++ = at(cursor++);
   i = ntohl(i);

   bitCursor = 0;
   return *this;
}

Packet &Packet::readUInt(unsigned &i)
{
   u_char *p = (u_char*)&i;
   
   *p++ = at(cursor++);
   *p++ = at(cursor++);
   *p++ = at(cursor++);
   *p++ = at(cursor++);
   i = ntohl(i);

   bitCursor = 0;
   return *this;
}

Packet &Packet::readLong(unsigned long &l)
{
   u_char *p = (u_char*)&l;
   
   *p++ = at(cursor++);
   *p++ = at(cursor++);
   *p++ = at(cursor++);
   *p++ = at(cursor++);
   l = ntohl(l);

   bitCursor = 0;
   return *this;
}

Packet &Packet::readFloat(float &f)
{
   u_char *p = (u_char*)&f;
   
   *p++ = at(cursor++);
   *p++ = at(cursor++);
   *p++ = at(cursor++);
   *p++ = at(cursor++);

   bitCursor = 0;
   return *this;
}

Packet &Packet::readCStr(char *str)
{
   u_char temp;

   do {
      temp = at(cursor++);
      *str++ = temp;
   } while (temp != 0);

   bitCursor = 0;
   return *this;
}

Packet &Packet::readStdStr(std::string &str)
{
   str = string((const char *)&at(cursor));
   cursor += str.size()+1;
   bitCursor = 0;
   return *this;
}

Packet &Packet::readData(unsigned char *data, int length)
{
   for (int i = 0; i < length; i++)
      *data++ = at(cursor++);

   bitCursor = 0;
   return *this;
}

Packet &Packet::reset(int size)
{
   assign(size, 0);
   cursor = 0;
   bitCursor = 0;
   return *this;
}

Packet &Packet::setCursor(int pos)
{
   cursor = pos;
   bitCursor = 0;
   return *this;
}




/************* SelectSet class *************/

struct SelectInfo {
   vector<int> sds;
};

SelectSet::SelectSet()
   : info(new SelectInfo)
{

}

void SelectSet::add(int sd)
{
   info->sds.push_back(sd);
}

void SelectSet::remove(int sd)
{
   vector<int>::iterator itr = find(info->sds.begin(), info->sds.end(), sd);
   if (itr != info->sds.end()) {
      info->sds.erase(itr);
   }
}

vector<int> SelectSet::select(int ms)
{
   timeval timeout;
   int selectVal;
   fd_set reads;
   u_long largest = 0;
   vector<int> ret;

   FD_ZERO(&reads);
   for (size_t i = 0; i < info->sds.size(); i++) {
      FD_SET(info->sds[i], &reads);
      if ((u_long)info->sds[i] > largest)
         largest = info->sds[i];
   }

   if (ms >= 0) {
      timeout.tv_sec = ms / 1000;
      timeout.tv_usec = (ms % 1000) * 1000;
      selectVal = sockSelect(largest+1, &reads, 0, 0, &timeout);
   } else {
      selectVal = sockSelect(largest+1, &reads, 0, 0, 0);
   }

   if (selectVal == SOCKET_ERROR)
      sockError();

   for (size_t i = 0; i < info->sds.size(); i++) {
      if (FD_ISSET(info->sds[i], &reads))
         ret.push_back(info->sds[i]);
   }

   return ret;
}

/*struct SelectInfo {
   vector<Connection> conns;
   u_long largest;
};

SelectSet::SelectSet()
{
   info.reset(new SelectInfo);
   info->largest = -1;
}

void SelectSet::add(Connection c)
{
   u_long s = c.getSocket();
   info->conns.push_back(c);
   if (s > info->largest)
      info->largest = s;
}

void SelectSet::remove(Connection c)
{
   u_long s = c.getSocket();
   info->conns.erase(find(info->conns.begin(), info->conns.end(), c));
   if (s == info->largest) {
      info->largest = -1;
      for(size_t i = 0; i < info->conns.size(); i++)
         if ((s = info->conns[i].getSocket()) > info->largest)
            info->largest = s;
   }
}

vector<Connection> SelectSet::select(int ms)
{
   timeval timeout;
   int selectVal;
   fd_set reads;
   vector<Connection> ret;

   FD_ZERO(&reads);
   for (size_t i = 0; i < info->conns.size(); i++)
      FD_SET(info->conns[i].getSocket(), &reads);

   if (ms >= 0) {
      timeout.tv_sec = ms / 1000;
      timeout.tv_usec = (ms % 1000) * 1000;
      selectVal = sockSelect(info->largest+1, &reads, 0, 0, &timeout);
   } else {
      selectVal = sockSelect(info->largest+1, &reads, 0, 0, 0);
   }

   if (selectVal == SOCKET_ERROR)
      sockError();
   else if (selectVal == 0)
      return ret;

   for (size_t i = 0; i < info->conns.size(); i++) {
      if (FD_ISSET(info->conns[i].getSocket(), &reads))
         ret.push_back(info->conns[i]);
   }

   return vector<Connection>();
}

vector<Connection> SelectSet::nbRead(int ms)
{
   return vector<Connection>();
}

void SelectSet::removeDisconnects()
{
   
}*/


} // End sock namespace





/********************************************/
/************ Utility Functions *************/
/********************************************/
namespace { 
#ifdef WIN32
   void setupErrors()
   {
      if (errors.size() == 0) {
         errors[0]                   = "No error";
         errors[WSAEINTR]            = "Interrupted system call";
         errors[WSAEBADF]            = "Bad file number";
         errors[WSAEACCES]           = "Permission denied";
         errors[WSAEFAULT]           = "Bad address";
         errors[WSAEINVAL]           = "Invalid argument";
         errors[WSAEMFILE]           = "Too many open sockets";
         errors[WSAEWOULDBLOCK]      = "Operation would block";
         errors[WSAEINPROGRESS]      = "Operation now in progress";
         errors[WSAEALREADY]         = "Operation already in progress";
         errors[WSAENOTSOCK]         = "Socket operation on non-socket";
         errors[WSAEDESTADDRREQ]     = "Destination address required";
         errors[WSAEMSGSIZE]         = "Message too long";
         errors[WSAEPROTOTYPE]       = "Protocol wrong type for socket";
         errors[WSAENOPROTOOPT]      = "Bad protocol option";
         errors[WSAEPROTONOSUPPORT]  = "Protocol not supported";
         errors[WSAESOCKTNOSUPPORT]  = "Socket type not supported";
         errors[WSAEOPNOTSUPP]       = "Operation not supported on socket";
         errors[WSAEPFNOSUPPORT]     = "Protocol family not supported";
         errors[WSAEAFNOSUPPORT]     = "Address family not supported";
         errors[WSAEADDRINUSE]       = "Address already in use";
         errors[WSAEADDRNOTAVAIL]    = "Can't assign requested address";
         errors[WSAENETDOWN]         = "Network is down";
         errors[WSAENETUNREACH]      = "Network is unreachable";
         errors[WSAENETRESET]        = "Net connection reset";
         errors[WSAECONNABORTED]     = "Software caused connection abort";
         errors[WSAECONNRESET]       = "Connection reset by peer";
         errors[WSAENOBUFS]          = "No buffer space available";
         errors[WSAEISCONN]          = "Socket is already connected";
         errors[WSAENOTCONN]         = "Socket is not connected";
         errors[WSAESHUTDOWN]        = "Can't send after socket shutdown";
         errors[WSAETOOMANYREFS]     = "Too many references]  can't splice";
         errors[WSAETIMEDOUT]        = "Connection timed out";
         errors[WSAECONNREFUSED]     = "Connection refused";
         errors[WSAELOOP]            = "Too many levels of symbolic links";
         errors[WSAENAMETOOLONG]     = "File name too long";
         errors[WSAEHOSTDOWN]        = "Host is down";
         errors[WSAEHOSTUNREACH]     = "No route to host";
         errors[WSAENOTEMPTY]        = "Directory not empty";
         errors[WSAEPROCLIM]         = "Too many processes";
         errors[WSAEUSERS]           = "Too many users";
         errors[WSAEDQUOT]           = "Disc quota exceeded";
         errors[WSAESTALE]           = "Stale NFS file handle";
         errors[WSAEREMOTE]          = "Too many levels of remote in path";
         errors[WSASYSNOTREADY]      = "Network system is unavailable";
         errors[WSAVERNOTSUPPORTED]  = "Winsock version out of range";
         errors[WSANOTINITIALISED]   = "WSAStartup not yet called";
         errors[WSAEDISCON]          = "Graceful shutdown in progress";
         errors[WSAHOST_NOT_FOUND]   = "Host not found";
         errors[WSANO_DATA]          = "No host data of that type was found";
      }
   }
#endif
   
   u_long getAddress(const char* pcHost)
   {
      if (pcHost == 0)
         return INADDR_ANY;

      u_long nRemoteAddr = inet_addr(pcHost);
      if (nRemoteAddr == INADDR_NONE) {
         // pcHost isn't a dotted IP, so resolve it through DNS
         hostent* pHE = gethostbyname(pcHost);
         if (pHE == 0) {
            return INADDR_NONE;
         }
         nRemoteAddr = *((u_long*)pHE->h_addr_list[0]);
      }

      return nRemoteAddr;
   }

   void shutdownSocket(SOCKET sd) {
      if (shutdown(sd, SD_SEND) == SOCKET_ERROR)
         sockError();

      /*
      char buf[1024];
      int bytesRead;

      do {
         bytesRead = recv(sd, buf, 512, 0);
         if (bytesRead == SOCKET_ERROR)
            sockError();
      } while (bytesRead != 0); */

      if (close(sd) == SOCKET_ERROR)
         sockError();
   }
}
