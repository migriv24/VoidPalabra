/* Proves an embedding project can use the library end to end without including a
 * single cJSON header of its own: a replica round-trips through bytes (cJSON parse
 * and print, inside the library), and names a version. With
 * VOIDPALABRA_VENDOR_CJSON=OFF this only links if the host's cJSON satisfies the
 * library — which is the property being checked. */
#include "voidpalabra/canonical.hpp"
#include "voidpalabra/replica.hpp"

#include <cstdio>
#include <string>

using namespace voidpalabra;

int main() {
    Replica a;
    std::string why;
    if (!Replica::create("embed-check-replica-1", a, &why)) {
        std::printf("create: %s\n", why.c_str());
        return 1;
    }
    Replica back;
    if (!Replica::from_bytes(a.to_bytes(), back, &why)) {
        std::printf("round trip: %s\n", why.c_str());
        return 1;
    }
    Doc flat = back.flatten();
    std::string v = version_name(flat.root);
    std::printf("embedded Void Palabra works: %s\n", v.c_str());
    return v.rfind("v:", 0) == 0 ? 0 : 1;
}
