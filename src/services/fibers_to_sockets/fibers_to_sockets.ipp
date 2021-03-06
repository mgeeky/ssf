#ifndef SSF_SERVICES_FIBERS_TO_SOCKETS_FIBERS_TO_SOCKETS_IPP_
#define SSF_SERVICES_FIBERS_TO_SOCKETS_FIBERS_TO_SOCKETS_IPP_

#include <ssf/log/log.h>

#include "common/error/error.h"

#include "services/fibers_to_sockets/session.h"

namespace ssf {
namespace services {
namespace fibers_to_sockets {

template <typename Demux>
FibersToSockets<Demux>::FibersToSockets(boost::asio::io_service& io_service,
                                        Demux& fiber_demux,
                                        LocalPortType local_port,
                                        const std::string& ip,
                                        RemotePortType remote_port)
    : ssf::BaseService<Demux>::BaseService(io_service, fiber_demux),
      remote_port_(remote_port),
      ip_(ip),
      local_port_(local_port),
      fiber_acceptor_(io_service) {}

template <typename Demux>
void FibersToSockets<Demux>::start(boost::system::error_code& ec) {
  FiberEndpoint ep(this->get_demux(), local_port_);
  fiber_acceptor_.bind(ep, ec);
  if (ec) {
    SSF_LOG("microservice", error,
            "[stream_forwarder]: cannot bind fiber "
            "acceptor to fiber port {}",
            local_port_);
    return;
  }

  fiber_acceptor_.listen(boost::asio::socket_base::max_connections, ec);
  if (ec) {
    SSF_LOG("microservice", error,
            "[stream_forwarder]: acceptor cannot listen on port {}",
            local_port_);
    return;
  }

  // Resolve the given address
  Tcp::resolver resolver(this->get_io_service());
  Tcp::resolver::query query(ip_, std::to_string(remote_port_));
  Tcp::resolver::iterator iterator(resolver.resolve(query, ec));
  if (ec) {
    SSF_LOG("microservice", error,
            "[stream_forwarder]: cannot resolve remote TCP endpoint <{}:{}>",
            ip_, remote_port_);
    return;
  }

  remote_endpoint_ = *iterator;

  SSF_LOG("microservice", info,
          "[stream_forwarder]: start "
          "forwarding stream fiber from fiber port {} to {}:{}",
          local_port_, ip_, remote_port_);

  this->AsyncAcceptFibers();
}

template <typename Demux>
void FibersToSockets<Demux>::stop(boost::system::error_code& ec) {
  SSF_LOG("microservice", debug, "[stream_forwarder]: stop");
  ec.assign(::error::success, ::error::get_ssf_category());

  fiber_acceptor_.close();
  manager_.stop_all();
}

template <typename Demux>
uint32_t FibersToSockets<Demux>::service_type_id() {
  return kFactoryId;
}

template <typename Demux>
void FibersToSockets<Demux>::StopSession(BaseSessionPtr session,
                                         boost::system::error_code& ec) {
  manager_.stop(session, ec);
}

template <typename Demux>
void FibersToSockets<Demux>::AsyncAcceptFibers() {
  SSF_LOG("microservice", trace,
          "[stream_forwarder]: accept new fiber connections");

  auto self = this->shared_from_this();
  FiberPtr new_connection = std::make_shared<Fiber>(
      this->get_io_service(), FiberEndpoint(this->get_demux(), 0));
  auto on_fiber_accept = [this, self,
                          new_connection](const boost::system::error_code& ec) {
    FiberAcceptHandler(new_connection, ec);
  };
  fiber_acceptor_.async_accept(*new_connection, std::move(on_fiber_accept));
}

template <typename Demux>
void FibersToSockets<Demux>::FiberAcceptHandler(
    FiberPtr fiber_connection, const boost::system::error_code& ec) {
  if (ec) {
    SSF_LOG("microservice", debug,
            "[stream_forwarder]: error accepting new connection: {} ({})",
            ec.message(), ec.value());
    return;
  }

  if (fiber_acceptor_.is_open()) {
    this->AsyncAcceptFibers();
  }

  std::shared_ptr<Tcp::socket> socket =
      std::make_shared<Tcp::socket>(this->get_io_service());
  socket->async_connect(
      remote_endpoint_,
      std::bind(&FibersToSockets::TcpSocketConnectHandler, this->SelfFromThis(),
                socket, fiber_connection, std::placeholders::_1));
}

template <typename Demux>
void FibersToSockets<Demux>::TcpSocketConnectHandler(
    std::shared_ptr<Tcp::socket> socket, FiberPtr fiber_connection,
    const boost::system::error_code& ec) {
  if (ec) {
    SSF_LOG("microservice", error,
            "[stream_forwarder]: error connecting to remote socket");
    fiber_connection->close();
    return;
  }

  auto session = Session<Demux, Fiber, Tcp::socket>::create(
      this->SelfFromThis(), std::move(*fiber_connection), std::move(*socket));
  boost::system::error_code start_ec;
  manager_.start(session, start_ec);
  if (start_ec) {
    SSF_LOG("microservice", error, "[stream_forwarder]: cannot start session");
    start_ec.clear();
    session->stop(start_ec);
  }
}

}  // fibers_to_sockets
}  // services
}  // ssf

#endif  // SSF_SERVICES_FIBERS_TO_SOCKETS_FIBERS_TO_SOCKETS_IPP_
