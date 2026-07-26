#include "hermes/ComposerAssistantConnector.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace dawhermes::hermes {

namespace {

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool isLoopbackHost(std::string_view host)
{
    const auto normalized = toLower(std::string(host));
    if (normalized == "localhost" || normalized == "127.0.0.1" || normalized == "::1") {
        return true;
    }

    if (normalized.rfind("127.", 0) == 0) {
        return true;
    }

    return false;
}

#ifdef _WIN32
std::string socketErrorMessage(int code)
{
    std::ostringstream stream;
    stream << "Socket error " << code;
    return stream.str();
}
#endif

}  // namespace

ComposerAssistantSettings ComposerAssistantConnector::defaultSettings() const
{
    return ComposerAssistantSettings {};
}

ComposerAssistantProbeResult ComposerAssistantConnector::validateSettings(
    const ComposerAssistantSettings& settings) const
{
    if (settings.host.empty()) {
        return ComposerAssistantProbeResult { false, "Host cannot be empty." };
    }

    if (settings.port <= 0 || settings.port > 65535) {
        return ComposerAssistantProbeResult { false, "Port must be in range 1..65535." };
    }

    if (settings.timeoutMs < 100 || settings.timeoutMs > 15000) {
        return ComposerAssistantProbeResult { false, "Timeout must be in range 100..15000 ms." };
    }

    if (settings.requireLoopbackHost && !isLoopbackHost(settings.host)) {
        return ComposerAssistantProbeResult {
            false,
            "Loopback-only safety is enabled; choose localhost/127.0.0.1/::1 or disable loopback-only mode."
        };
    }

    return ComposerAssistantProbeResult { true, "Settings are valid." };
}

ComposerAssistantProbeResult ComposerAssistantConnector::probe(const ComposerAssistantSettings& settings) const
{
    const auto validation = validateSettings(settings);
    if (!validation.ok) {
        return validation;
    }

    if (!settings.enabled) {
        return ComposerAssistantProbeResult {
            false,
            "Connector is disabled. Enable it in Composer Assistant settings before probing."
        };
    }

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return ComposerAssistantProbeResult { false, "Failed to initialize WinSock." };
    }

    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    const auto portText = std::to_string(settings.port);
    const int lookupResult = getaddrinfo(settings.host.c_str(), portText.c_str(), &hints, &result);
    if (lookupResult != 0) {
        WSACleanup();
        std::ostringstream stream;
        stream << "Address resolution failed for " << settings.host << ":" << settings.port
               << " (" << lookupResult << ")";
        return ComposerAssistantProbeResult { false, stream.str() };
    }

    bool connected = false;
    std::string failureReason = "Connection attempt failed.";

    for (auto* entry = result; entry != nullptr; entry = entry->ai_next) {
        SOCKET sock = socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
        if (sock == INVALID_SOCKET) {
            failureReason = "Socket creation failed.";
            continue;
        }

        u_long nonBlocking = 1;
        if (ioctlsocket(sock, FIONBIO, &nonBlocking) != 0) {
            failureReason = "Failed to configure non-blocking socket.";
            closesocket(sock);
            continue;
        }

        const int connectResult = connect(sock, entry->ai_addr, static_cast<int>(entry->ai_addrlen));
        if (connectResult == 0) {
            connected = true;
            closesocket(sock);
            break;
        }

        const int connectError = WSAGetLastError();
        if (connectError == WSAEWOULDBLOCK || connectError == WSAEINPROGRESS) {
            fd_set writeSet;
            FD_ZERO(&writeSet);
            FD_SET(sock, &writeSet);

            timeval timeout;
            timeout.tv_sec = settings.timeoutMs / 1000;
            timeout.tv_usec = (settings.timeoutMs % 1000) * 1000;

            const int selectResult = select(0, nullptr, &writeSet, nullptr, &timeout);
            if (selectResult > 0 && FD_ISSET(sock, &writeSet)) {
                int socketError = 0;
                int optionSize = sizeof(socketError);
                if (getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &optionSize) == 0
                    && socketError == 0) {
                    connected = true;
                    closesocket(sock);
                    break;
                }

                failureReason = socketErrorMessage(socketError);
            } else if (selectResult == 0) {
                failureReason = "Connection timed out.";
            } else {
                failureReason = "Socket select failed.";
            }
        } else {
            failureReason = socketErrorMessage(connectError);
        }

        closesocket(sock);
    }

    freeaddrinfo(result);
    WSACleanup();

    if (connected) {
        std::ostringstream stream;
        stream << "Connected to Composer Assistant endpoint at " << settings.host << ":" << settings.port
               << ".";
        return ComposerAssistantProbeResult { true, stream.str() };
    }

    return ComposerAssistantProbeResult { false, failureReason };
#else
    (void)settings;
    return ComposerAssistantProbeResult { false, "Probe is currently implemented for Windows builds only." };
#endif
}

}  // namespace dawhermes::hermes
