#include <mln/style/terrain.hpp>
#include <mln/style/terrain_impl.hpp>
#include <mln/style/terrain_observer.hpp>
#include <mln/style/conversion_impl.hpp>
#include <mln/style/conversion/constant.hpp>

#include <utility>

namespace mln {
namespace style {

namespace {
TerrainObserver nullObserver;
} // namespace

Terrain::Terrain(Immutable<Terrain::Impl> impl_)
    : impl(std::move(impl_)),
      observer(&nullObserver) {}

Terrain::Terrain()
    : Terrain(makeMutable<Impl>()) {}

Terrain::~Terrain() = default;

void Terrain::setObserver(TerrainObserver* observer_) {
    observer = observer_ ? observer_ : &nullObserver;
}

Mutable<Terrain::Impl> Terrain::mutableImpl() const {
    return makeMutable<Impl>(*impl);
}

double Terrain::getDefaultExaggeration() {
    return 1.0;
}

double Terrain::getExaggeration() const {
    return impl->exaggeration;
}

void Terrain::setExaggeration(double exaggeration_) {
    auto mutableImpl_ = mutableImpl();
    mutableImpl_->exaggeration = exaggeration_;
    impl = std::move(mutableImpl_);
    observer->onTerrainChanged(*this);
}

const std::string& Terrain::getSource() const {
    return impl->source;
}

void Terrain::setSource(std::string source_) {
    auto mutableImpl_ = mutableImpl();
    mutableImpl_->source = std::move(source_);
    impl = std::move(mutableImpl_);
    observer->onTerrainChanged(*this);
}

bool Terrain::isEnabled() const {
    return !impl->source.empty();
}

std::optional<conversion::Error> Terrain::setProperty(const std::string& name, const conversion::Convertible& value) {
    conversion::Error error;
    if (name == "source") {
        const auto converted = conversion::convert<std::string>(value, error);
        if (!converted) {
            return error;
        }
        setSource(*converted);
        return std::nullopt;
    }
    if (name == "exaggeration") {
        const auto converted = conversion::convert<float>(value, error);
        if (!converted) {
            return error;
        }
        setExaggeration(*converted);
        return std::nullopt;
    }
    return conversion::Error{"terrain doesn't support this property"};
}

StyleProperty Terrain::getProperty(const std::string& name) const {
    if (name == "source") {
        return {conversion::makeValue(impl->source), StyleProperty::Kind::Constant};
    }
    if (name == "exaggeration") {
        return {conversion::makeValue(impl->exaggeration), StyleProperty::Kind::Constant};
    }
    return {};
}

} // namespace style
} // namespace mln
