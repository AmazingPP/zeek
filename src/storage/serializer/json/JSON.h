// See the file "COPYING" in the main distribution directory for copyright.

#pragma once

#include "zeek/storage/Serializer.h"

namespace zeek::storage::serializer::json {

class JSON final : public Serializer {
public:
    static std::unique_ptr<Serializer> Instantiate();

    JSON();
    ~JSON() override = default;

    std::optional<byte_buffer> Serialize(ValPtr val) override;
    zeek::expected<ValPtr, std::string> Unserialize(byte_buffer_span buf, TypePtr type) override;
};

} // namespace zeek::storage::serializer::json
