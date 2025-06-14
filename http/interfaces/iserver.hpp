#ifndef ISERVER_HPP
#define ISERVER_HPP

#include <string>
#include <memory>

class IServer {
public:
    virtual ~IServer() = default;

	virtual void setupServer(int port, int timeoutMs) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    virtual std::unique_ptr<std::string> readRequest(int idleTime) = 0;

    virtual int getPort() const = 0;
};

#endif // ISERVER_HPP
