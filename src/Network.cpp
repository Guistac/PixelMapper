#include "Network.h"
#include <algorithm>
#include <future>
#include <iostream>
#include <string>

namespace Logger {
    inline void log(std::ostream& os, const std::string& level, std::string fmt, const std::string& arg) {
        size_t pos = fmt.find("{}");
        if (pos != std::string::npos) {
            fmt.replace(pos, 2, arg);
        }
        os << "[" << level << "] " << fmt << std::endl;
    }

    inline void debug(const std::string& msg) {
        std::cout << "[Network Debug] " << msg << std::endl;
    }
    inline void warn(const std::string& msg) {
        std::cerr << "[Network Warn] " << msg << std::endl;
    }
    inline void warn(const std::string& fmt, const std::string& arg) {
        log(std::cerr, "Network Warn", fmt, arg);
    }
    inline void error(const std::string& msg) {
        std::cerr << "[Network Error] " << msg << std::endl;
    }
    inline void error(const std::string& fmt, const std::string& arg) {
        log(std::cerr, "Network Error", fmt, arg);
    }
}

namespace Network{

asio::io_context io_context;
std::thread io_context_handler;
bool b_initialized;

// Keep io_context alive until we explicitly release the guard
std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> work_guard;

void init() {
	// Create a work guard to prevent io_context.run() from returning immediately
	work_guard = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(
		asio::make_work_guard(io_context)
	);

	io_context_handler = std::thread([&]() {
		//pthread_setname_np("Asio Network Thread");
		b_initialized = true;
		Logger::debug("===== Started IP Network IO Context");
		io_context.run();  // will now keep running as long as work_guard exists
		b_initialized = false;
		Logger::debug("===== Stopped IP Network IO Context");
	});

}

void terminate() {
	if(work_guard) work_guard->reset();  // allow run() to exit when no pending operations
	io_context.stop();
	if(io_context_handler.joinable()) io_context_handler.join();
}

//bind the socket to be able to listen on the listening port
std::unique_ptr<asio::ip::udp::socket> getUdpSocket(int listeningPort, std::vector<int> remoteIp, int remotePort) {
	using namespace asio::ip;
    std::unique_ptr<udp::socket> socket = nullptr;
    if (remoteIp.size() != 4) return socket;
    for (int octet : remoteIp) if (octet > 255 || octet < 0) return socket;
    char ip[32];
    snprintf(ip, sizeof(ip), "%i.%i.%i.%i", remoteIp[0], remoteIp[1], remoteIp[2], remoteIp[3]);

    try {
        asio::ip::address_v4 remoteIp = make_address_v4(ip);
        udp::endpoint localEndpoint(udp::v4(), listeningPort);
        udp::endpoint remoteEndpoint(remoteIp, remotePort);
        socket = std::make_unique<udp::socket>(io_context, localEndpoint);
        socket->async_connect(remoteEndpoint, [](asio::error_code) {});
    }
    catch (std::exception e) {
        Logger::error("UDP Socket Creation Network Error: {}", e.what());
        return nullptr;
    }
    return socket;
}

//connect to the socket for sending only
std::unique_ptr<asio::ip::udp::socket> getUdpSocket(std::vector<int> remoteIp, int remotePort){
	std::unique_ptr<asio::ip::udp::socket> socket = nullptr;
	if (remoteIp.size() != 4) return socket;
	for (int octet : remoteIp) if (octet > 255 || octet < 0) return socket;
	char ip[32];
	snprintf(ip, sizeof(ip), "%i.%i.%i.%i", remoteIp[0], remoteIp[1], remoteIp[2], remoteIp[3]);

	try {
		asio::ip::address_v4 remoteIp = asio::ip::make_address_v4(ip);
		asio::ip::udp::endpoint remoteEndpoint(remoteIp, remotePort);
		socket = std::make_unique<asio::ip::udp::socket>(io_context);
		socket->async_connect(remoteEndpoint, [](asio::error_code) {});
	}
	catch (std::exception e) {
		Logger::error("UDP Socket Creation Network Error: {}", e.what());
		return nullptr;
	}
	return socket;
}

std::unique_ptr<asio::ip::udp::socket> getUdpBroadcastSocket(){
    asio::error_code error;
    try{
        auto socket = std::make_unique<asio::ip::udp::socket>(io_context);
        socket->open(asio::ip::udp::v4(), error);
        if(error) return nullptr;
        socket->set_option(asio::ip::udp::socket::reuse_address(true));
        socket->set_option(asio::socket_base::broadcast(true));
        return socket;
    }catch(std::exception e){
        Logger::warn("Coult not create udp broadcast socket : {}", e.what());
        return nullptr;
    }
}


std::unique_ptr<asio::ip::udp::socket> getUdpMulticastSocket(std::vector<int> localIp, std::vector<int> remoteIp, uint16_t remotePort){
        
    asio::error_code error;
    uint32_t local_u32 = localIp[0] << 24 | localIp[1] << 16 | localIp[2] << 8 | localIp[3];
    uint32_t remote_u32 = remoteIp[0] << 24 | remoteIp[1] << 16 | remoteIp[2] << 8 | remoteIp[3];

    try{
        
        asio::ip::udp::endpoint localEndpoint(asio::ip::address_v4(local_u32), remotePort);
        asio::ip::udp::endpoint remoteEndpoint(asio::ip::address_v4(remote_u32), remotePort);
        
        auto socket = std::make_unique<asio::ip::udp::socket>(io_context, localEndpoint);
        socket->async_connect(remoteEndpoint, [](asio::error_code){});
        
        return socket;
    }catch(std::exception e){
        return nullptr;
    }
}












// Helper: convert vector<int> to asio::ip::address_v4
asio::ip::address_v4 vecToAddress(const std::vector<int>& ip) {
	if (ip.size() != 4) throw std::runtime_error("Invalid IPv4 address");
	return asio::ip::address_v4(
		(ip[0] << 24) | (ip[1] << 16) | (ip[2] << 8) | ip[3]
	);
}

// Sender-only socket, not bound to a receiving port
std::unique_ptr<asio::ip::udp::socket> getSenderUdpSocket() {
	auto socket = std::make_unique<asio::ip::udp::socket>(io_context);
	socket->open(asio::ip::udp::v4());
	return socket;
}

// Receiver + sender socket, bound to a receiving port
std::unique_ptr<asio::ip::udp::socket> getReceiverSenderUdpSocket(int listeningPort) {
	auto socket = std::make_unique<asio::ip::udp::socket>(io_context);
	socket->open(asio::ip::udp::v4());
	//socket->set_option(asio::socket_base::reuse_address(true));
	socket->bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), listeningPort));
	return socket;
}














bool isInitialized(){
	return b_initialized;
}





bool UdpSocket::isOpen() const {
	return openFlag.load();
}

// ---------------------------------------------------------
// synchronous open
// ---------------------------------------------------------
void UdpSocket::open() {
	std::promise<void> p;
	auto f = p.get_future();

	asio::post(io_context, [this, &p]() {
		internalOpen();
		p.set_value();
	});

	f.wait(); // wait for internalOpen() to finish
}

// ---------------------------------------------------------
// synchronous startReceiving
// ---------------------------------------------------------
void UdpSocket::startReceiving(
	uint16_t port,
	std::function<void(const uint8_t*, size_t, const asio::ip::udp::endpoint&)> cb)
{
	std::promise<void> p;
	auto f = p.get_future();

	asio::post(io_context, [this, port, cb = std::move(cb), &p]() mutable {
		internalStartReceiving(port, std::move(cb));
		p.set_value();
	});

	f.wait(); // wait for internalStartReceiving() to finish
}

void UdpSocket::startMulticastReceiving(
	uint16_t port,
	uint32_t multicastGroup,
	uint32_t localInterface,
	std::function<void(const uint8_t*, size_t, const asio::ip::udp::endpoint&)> cb)
{
	std::promise<void> p;
	auto f = p.get_future();

	asio::post(io_context, [this, port, multicastGroup, localInterface, cb = std::move(cb), &p]() mutable {
		internalStartMulticastReceiving(port, multicastGroup, localInterface, std::move(cb));
		p.set_value();
	});

	f.wait();
}

// ---------------------------------------------------------
// synchronous close
// ---------------------------------------------------------
void UdpSocket::close() {
	std::promise<void> p;
	auto f = p.get_future();

	asio::post(io_context, [this, &p]() {
		internalClose();
		p.set_value();
	});

	f.wait(); // wait for internalClose() to finish
}

// ---------------------------------------------------------
// SEND
// ---------------------------------------------------------
void UdpSocket::send(const void* data, size_t size, const asio::ip::udp::endpoint& dest) {
	auto buf = std::make_shared<std::vector<uint8_t>>(
		(uint8_t*)data, (uint8_t*)data + size
	);

	auto errCb = onErrorCallback; // copy to avoid lifetime race issues

	asio::post(io_context, [this, buf, dest, errCb]() {
		if (!socket) return;

		Logger::debug("ASIO sending to " + dest.address().to_string() + ":" + std::to_string(dest.port()));

		socket->async_send_to(
			asio::buffer(*buf),
			dest,
			[buf, errCb](const asio::error_code& ec, size_t /*sent*/) {
				if (ec) {
					Logger::error("UDP send error: {}", ec.message());
					if (errCb) {
						errCb(ec.message());
					}
				}
			}
		);
	});
}

void UdpSocket::send(const void* data, size_t size, uint32_t dest_ipv4, uint16_t dest_port){
	asio::ip::udp::endpoint endpoint(asio::ip::make_address_v4(dest_ipv4), dest_port);
	send(data, size, endpoint);
}

bool UdpSocket::bind(uint32_t localIp, uint16_t port) {
	std::promise<bool> p;
	auto f = p.get_future();

	asio::post(io_context, [this, localIp, port, &p]() {
		bool success = internalBind(localIp, port);
		p.set_value(success);
	});

	return f.get();
}

uint16_t UdpSocket::getLocalPort() {
	std::promise<uint16_t> p;
	auto f = p.get_future();

	asio::post(io_context, [this, &p]() {
		if (!socket) {
			p.set_value(0);
			return;
		}
		asio::error_code ec;
		auto endpoint = socket->local_endpoint(ec);
		p.set_value(ec ? 0 : endpoint.port());
	});

	return f.get();
}

std::string UdpSocket::getLocalAddress() {
	std::promise<std::string> p;
	auto f = p.get_future();

	asio::post(io_context, [this, &p]() {
		if (!socket) {
			p.set_value("0.0.0.0");
			return;
		}
		asio::error_code ec;
		auto endpoint = socket->local_endpoint(ec);
		p.set_value(ec ? "0.0.0.0" : endpoint.address().to_string());
	});

	return f.get();
}

void UdpSocket::setErrorCallback(std::function<void(const std::string&)> cb) {
	onErrorCallback = std::move(cb);
}

bool UdpSocket::internalBind(uint32_t localIp, uint16_t port) {
	if (!socket) {
		internalOpen();
		if (!socket) return false;
	}
	asio::error_code ec;
	socket->set_option(asio::socket_base::reuse_address(true), ec);
	socket->set_option(asio::socket_base::broadcast(true), ec);
	socket->bind(asio::ip::udp::endpoint(asio::ip::address_v4(localIp), port), ec);
	if (ec) {
		Logger::warn("UDP bind error: {}", ec.message());
		return false;
	}
	return true;
}

// =====================================================================
// INTERNAL OPEN (NO BIND)
// =====================================================================
void UdpSocket::internalOpen() {
	if (socket) return; // already open

	socket = std::make_unique<asio::ip::udp::socket>(io_context);

	asio::error_code ec;
	socket->open(asio::ip::udp::v4(), ec);
	if (ec) {
		Logger::warn("UDP open error: {}", ec.message());
		socket.reset();
		return;
	}

	openFlag = true;
}

// =====================================================================
// INTERNAL START RECEIVING (BIND + LOOP)
// =====================================================================
void UdpSocket::internalStartReceiving(
	uint16_t port,
	std::function<void(const uint8_t*, size_t, const asio::ip::udp::endpoint&)> cb)
{
	if (!socket) {
		internalOpen();
		if (!socket) return;
	}

	onReceive = std::move(cb);

	asio::error_code ec;
	socket->bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), port), ec);
	if (ec) {
		Logger::warn("UDP bind error: {}", ec.message());
		return;
	}

	receivingEnabled = true;
	doReceive();
}

void UdpSocket::internalStartMulticastReceiving(
	uint16_t port,
	uint32_t multicastGroup,
	uint32_t localInterface,
	std::function<void(const uint8_t*, size_t, const asio::ip::udp::endpoint&)> cb)
{
	if (!socket) {
		// We create the socket object but don't open it yet so we can set options
		socket = std::make_unique<asio::ip::udp::socket>(io_context);
	}

	onReceive = std::move(cb);
	asio::error_code ec;

	// 1. Open the socket manually
	socket->open(asio::ip::udp::v4(), ec);

	// 2. IMPORTANT FOR MACOS: Set both reuse_address AND reuse_port
	// macOS/BSD requires SO_REUSEPORT for multiple apps to bind to the same multicast port
	socket->set_option(asio::ip::udp::socket::reuse_address(true), ec);
	
	// Asio doesn't always have a shorthand for reuse_port, we use the native option
	asio::detail::socket_option::boolean<SOL_SOCKET, SO_REUSEPORT> reuse_port_option(true);
	socket->set_option(reuse_port_option, ec);

	// 3. Bind to 0.0.0.0 (Any) and the Port
	// On macOS, binding to the specific interface IP often blocks multicast receipt
	socket->bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), port), ec);
	
	if (ec) {
		Logger::error("Multicast bind error: {}", ec.message());
		return;
	}

	// 4. Join the group on the specific local interface
	auto groupAddr = asio::ip::make_address_v4(multicastGroup);
	auto interfaceAddr = asio::ip::make_address_v4(localInterface);

	socket->set_option(asio::ip::multicast::join_group(groupAddr, interfaceAddr), ec);
	
	if (ec) {
		Logger::error("Multicast join error: {}", ec.message());
		return;
	}

	receivingEnabled = true;
	doReceive();
}

// =====================================================================
// INTERNAL CLOSE
// =====================================================================
void UdpSocket::internalClose() {
	openFlag = false;
	receivingEnabled = false;
	onReceive = nullptr;

	if (!socket) return;

	asio::error_code ec;
	socket->cancel(ec);
	socket->close(ec);
	socket.reset();
}

// =====================================================================
// RECEIVE LOOP
// =====================================================================
void UdpSocket::doReceive() {
	if (!socket || !receivingEnabled) return;

	auto sender = std::make_shared<asio::ip::udp::endpoint>();

	socket->async_receive_from(
		asio::buffer(recvBuffer),
		*sender,
		[this, sender](const asio::error_code& ec, size_t bytes) {
			if (!ec && onReceive) {
				onReceive(recvBuffer.data(), bytes, *sender);
			}
			if (!ec && receivingEnabled && socket) {
				doReceive();
			}
		}
	);
}




uint32_t ipv4ToUint32(const std::string& ip) {
	uint8_t octets[4] = {0};
	int idx = 0;
	int num = 0;
	bool hasDigit = false;

	for (char c : ip) {
		if (c >= '0' && c <= '9') {
			hasDigit = true;
			num = num * 10 + (c - '0');
			if (num > 255) num = 255; // clamp
		} else {
			// delimiter or garbage → push current number
			if (hasDigit && idx < 4) {
				octets[idx++] = static_cast<uint8_t>(num);
			}
			num = 0;
			hasDigit = false;
		}
	}

	// push last segment
	if (hasDigit && idx < 4) {
		octets[idx++] = static_cast<uint8_t>(num);
	}

	// missing octets remain as 0

	return (static_cast<uint32_t>(octets[0]) << 24) |
		   (static_cast<uint32_t>(octets[1]) << 16) |
		   (static_cast<uint32_t>(octets[2]) << 8)  |
		   static_cast<uint32_t>(octets[3]);
}

std::string uint32ToIpv4(uint32_t ip) {
	uint8_t octet0 = (ip >> 24) & 0xFF;
	uint8_t octet1 = (ip >> 16) & 0xFF;
	uint8_t octet2 = (ip >> 8)  & 0xFF;
	uint8_t octet3 =  ip        & 0xFF;

	return std::to_string(octet0) + "." +
		   std::to_string(octet1) + "." +
		   std::to_string(octet2) + "." +
		   std::to_string(octet3);
}











}
