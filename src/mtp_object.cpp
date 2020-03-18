#include "mtp_object.hpp"
#include "mtp_storage.hpp"

namespace nq::mtp {

Object::Object(const ObjectInfo &info, const std::string &path, Object &parent):
    type(Object::format(info.format)), size(info.compressed_size), name(info.filename), path(std::move(path)), parent(&parent) { }

} // namespace nq::mtp
