#include <span>
#include <string>

#include "hcomm/base/logging.hpp"
#include "hcomm/transport/tcp/executor.hpp"
#include "hcomm/transport/tcp/socket.hpp"

using namespace hcomm;

void echoLoop(RefPtr<tcp::Socket>& sk, const std::shared_ptr<std::string>& buf, tcp::IOExecutor* exec) {
    exec->schedule(sk->read(*buf)
                       .timeout(std::chrono::seconds(10), ETIMEDOUT)
                       .orElse([sk](tcp::NetworkError& err) -> Result<ssize_t, tcp::NetworkError> {
                           if (err == ETIMEDOUT) {
                               HCOMM_LOG_WARN("timed-out! There is no byte received within 10s");
                           } else if (err == tcp::NetworkError::kEOF) {
                               HCOMM_LOG_INFO("client closed connection fd={}", sk->fd());
                           }
                           return Err(err);
                       })
                       .andThen([sk, buf, exec](ssize_t& nread) mutable {
                           HCOMM_LOG_INFO("read {} bytes from fd={}", nread, sk->fd());
                           return sk->write(std::span<char>(buf->data(), nread));
                       })
                       .andThen([sk, buf, exec](ssize_t& nwrite) mutable -> Result<void, tcp::NetworkError> {
                           if (nwrite > 0) {
                               HCOMM_LOG_INFO("write {} bytes to fd={}", nwrite, sk->fd());
                               echoLoop(sk, buf, exec);
                           }
                           return Ok();
                       }));
}

void acceptLoop(RefPtr<tcp::Listener> listener, tcp::IOExecutor* exec) {
    exec->schedule(listener->accept()
                       .andThen([listener, exec](RefPtr<tcp::Socket>& sk) mutable -> Result<void, tcp::NetworkError> {
                           HCOMM_LOG_INFO("accept new connection fd={}", sk->fd());
                           echoLoop(sk, std::make_shared<std::string>(1024, '\0'), exec);
                           acceptLoop(listener, exec);
                           return Ok();
                       })
                       .orElse([listener, exec](tcp::NetworkError& err) mutable -> Result<> {
                           HCOMM_LOG_ERROR("Accept failed with errno: {}, try again.", static_cast<int>(err));
                           acceptLoop(listener, exec);
                           return Ok();
                       })

    );
}

int main(int argc, char* argv[]) {
    tcp::IPv4Address addr(127, 0, 0, 1);
    int port = 8080;
    tcp::SocketAddress sa(addr, port);

    tcp::IOExecutor exec;
    auto result = tcp::Listener::bind(&exec, sa);
    if (!result) {
        return -1;
    }

    auto listener = std::move(result.value());
    acceptLoop(listener, &exec);

    HCOMM_LOG_INFO("Server started on 8080..., listen-fd={}", listener->fd());
    exec.loop();

    return 0;
}
