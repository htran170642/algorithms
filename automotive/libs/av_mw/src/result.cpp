#include "av/mw/result.hpp"

#include "av/service/someip.hpp"

namespace av::mw {

const char* describe(Failure failure) noexcept {
    switch (failure) {
        case Failure::NotAvailable:
            return "NOT_AVAILABLE";
        case Failure::Busy:
            return "BUSY";
        case Failure::Timeout:
            return "TIMEOUT";
        case Failure::Remote:
            return "REMOTE_ERROR";
        case Failure::WrongInterfaceVersion:
            return "WRONG_INTERFACE_VERSION";
        case Failure::Malformed:
            return "MALFORMED";
    }
    return "??";
}

const char* describe(service::ReturnCode code) noexcept {
    switch (code) {
        case service::ReturnCode::Ok:
            return "E_OK";
        case service::ReturnCode::NotOk:
            return "E_NOT_OK";
        case service::ReturnCode::UnknownService:
            return "E_UNKNOWN_SERVICE";
        case service::ReturnCode::UnknownMethod:
            return "E_UNKNOWN_METHOD";
        case service::ReturnCode::NotReady:
            return "E_NOT_READY";
        case service::ReturnCode::WrongProtocolVersion:
            return "E_WRONG_PROTOCOL_VERSION";
        case service::ReturnCode::WrongInterfaceVersion:
            return "E_WRONG_INTERFACE_VERSION";
        case service::ReturnCode::WrongMessageType:
            return "E_WRONG_MESSAGE_TYPE";
    }
    return "??";
}

}  // namespace av::mw
