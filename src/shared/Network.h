#pragma once

#include <asio.hpp>

namespace Network{


	extern asio::io_context io_context;

	void init();
	void terminate();
	bool isInitialized();
	std::unique_ptr<asio::ip::udp::socket> getUdpSocket(int listeningPort, std::vector<int> remoteIp, int remotePort);
	std::unique_ptr<asio::ip::udp::socket> getUdpBroadcastSocket();
    std::unique_ptr<asio::ip::udp::socket> getUdpMulticastSocket(std::vector<int> localIp, std::vector<int> remoteIp, uint16_t remotePort);

	std::unique_ptr<asio::ip::udp::socket> getSenderUdpSocket();
	std::unique_ptr<asio::ip::udp::socket> getReceiverSenderUdpSocket(int listeningPort);



class UdpSocket {
public:
	UdpSocket() = default;

	bool isOpen() const;

	void open(); // synchronous
	bool bind(uint32_t localIp, uint16_t port); // synchronous bind
	uint16_t getLocalPort();
	std::string getLocalAddress();
	void setErrorCallback(std::function<void(const std::string&)> cb);
	void startReceiving(
		uint16_t listenPort,
		std::function<void(const uint8_t*, size_t, const asio::ip::udp::endpoint&)> cb
	); // synchronous
	void startMulticastReceiving(
		uint16_t port,
		uint32_t multicastGroup,
		uint32_t localInterface,
		std::function<void(const uint8_t*, size_t, const asio::ip::udp::endpoint&)> cb
	);
	void close(); // synchronous
	void send(const void* data, size_t size, const asio::ip::udp::endpoint& dest);
	void send(const void* data, size_t size, uint32_t dest_ipv4, uint16_t dest_port);

private:
	std::unique_ptr<asio::ip::udp::socket> socket;
	std::array<uint8_t, 2048> recvBuffer;
	std::function<void(const uint8_t*, size_t, const asio::ip::udp::endpoint&)> onReceive;
	std::function<void(const std::string&)> onErrorCallback;
	std::atomic<bool> openFlag{false};
	bool receivingEnabled = false;

	void internalOpen();
	bool internalBind(uint32_t localIp, uint16_t port);
	void internalStartReceiving(
		uint16_t port,
		std::function<void(const uint8_t*, size_t, const asio::ip::udp::endpoint&)> cb
	);
	void internalStartMulticastReceiving(
		uint16_t port,
		uint32_t multicastGroup,
		uint32_t localInterface,
		std::function<void(const uint8_t*, size_t, const asio::ip::udp::endpoint&)> cb
	);
	void internalClose();
	void doReceive();
};



uint32_t ipv4ToUint32(const std::string& ip);
std::string uint32ToIpv4(uint32_t ip);


}
