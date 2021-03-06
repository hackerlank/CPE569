#include "packet.h"
#include <boost/shared_ptr.hpp>
#include <vector>

namespace sock {
   void setupSockets();
   void shutdownSockets();

   struct ConnectionInfo;
   struct ServerInfo;
   struct SelectInfo;

   class Socket {
   public:
      virtual unsigned long getSocket() const =0;
      virtual bool select(int ms = 0);
      virtual operator bool() const =0;
      virtual bool check() =0;
      virtual bool operator== (const Socket &other)
      {
         return this->getSocket() == other.getSocket();
      }
   protected:
      virtual void error() =0;
   };

   class Packet : public std::vector<unsigned char> {
   public:
      Packet(int size=0) 
         : std::vector<unsigned char>(size), cursor(0), bitCursor(0) {}

      Packet &writeBit(bool b);
      Packet &writeByte(unsigned char b);
      Packet &writeShort(unsigned short s);
      Packet &writeInt(int i);
      Packet &writeCStr(const char *str);
      Packet &writeStdStr(const std::string &str);
      Packet &writeData(const unsigned char *data, int length);
      Packet &writeInfo(const packet::info i);

      Packet &readBit(bool &b);
      Packet &readByte(unsigned char &b);
      Packet &readShort(unsigned short &s);
      Packet &readInt(int &i);
      Packet &readCStr(char *str);
      Packet &readStdStr(std::string &str);
      Packet &readData(unsigned char *data, int length);
      Packet &readInfo(packet::info &i);

      Packet &reset(int size = 0);
      Packet &setCursor(int pos);

   protected:
      int cursor, bitCursor;
      void checkSpace(int amount);
   };

   class Connection : public Socket {
   public:
      Connection(const char *host, int port);
      Connection(ConnectionInfo *info);
      bool send(const Packet &p);
      bool recv(Packet &p, int size = -1);
      void close();
      operator bool() const;
      unsigned long available();
      virtual unsigned long getSocket() const;
      virtual bool check();
   protected:
      virtual void error();
      boost::shared_ptr<ConnectionInfo> info;
   };

   class Server : public Socket {
   public:
      Server(int port, const char *addr = 0);
      void listen(int queueSize);
      Connection accept();
      virtual unsigned long getSocket() const;
      virtual bool check();
      virtual operator bool() const;
   protected:
      virtual void error();
      boost::shared_ptr<ServerInfo> info;
   };

   class SelectSet {
   public:
      SelectSet();
      void add(Connection c);
      void remove(Connection c);
      std::vector<Connection> select(int ms = 0);
      std::vector<Connection> nbRead(int ms = 0);
      void removeDisconnects();
   private:
      boost::shared_ptr<SelectInfo> info;
   };
}

