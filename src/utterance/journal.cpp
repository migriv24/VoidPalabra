/* journal.cpp — reading Void Core's command journal (VoidCore:SPEC.md §6.2).
 *
 * The narrowest file in the library on purpose: it knows the shape of Core's
 * export and nothing else. Everything downstream works on `JournalEntry`, so a
 * change to Core's JSON is a change to this file alone.
 *
 * Strict by design. Every field Core documents is required, and `slice` must be
 * one of the three named values. The temptation is to default a missing field and
 * carry on — but the entries here become the permanent, content-addressed record
 * of what happened, and an utterance built on a defaulted field is a confident
 * lie with a verifiable hash on it. A parse failure is recoverable; a wrong
 * utterance, once transmitted, is not.
 */
#include "voidpalabra/utterance.hpp"

#include "cJSON.h"

#include <string>
#include <vector>

namespace voidpalabra {

namespace {

const cJSON* item(const cJSON* obj, const char* key) {
    return cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(obj), key);
}

bool fail(std::string* error, const std::string& what) {
    if (error) *error = what;
    return false;
}

}  // namespace

bool parse_journal(const cJSON* array, std::vector<JournalEntry>& out,
                   std::string* error) {
    out.clear();
    if (!array || !cJSON_IsArray(const_cast<cJSON*>(array)))
        return fail(error, "journal export is not a JSON array");

    int index = 0;
    for (const cJSON* e = array->child; e; e = e->next, ++index) {
        const std::string at = "entry " + std::to_string(index) + ": ";
        if (!cJSON_IsObject(const_cast<cJSON*>(e)))
            return fail(error, at + "not an object");

        JournalEntry j;

        const cJSON* seq = item(e, "seq");
        if (!seq || !cJSON_IsNumber(seq)) return fail(error, at + "no numeric 'seq'");
        j.seq = static_cast<std::int64_t>(seq->valuedouble);

        const cJSON* command = item(e, "command");
        if (!command || !cJSON_IsString(command) || !command->valuestring)
            return fail(error, at + "no string 'command'");
        j.command = command->valuestring;

        const cJSON* verb = item(e, "verb");
        if (!verb || !cJSON_IsString(verb) || !verb->valuestring)
            return fail(error, at + "no string 'verb'");
        j.verb = verb->valuestring;

        /* `who` must be PRESENT and must be a string or null. Core emits an
         * explicit null rather than omitting the key, so an absent one means the
         * export came from something that is not the journal. */
        const cJSON* who = item(e, "who");
        if (!who) return fail(error, at + "no 'who' (null is required, not absence)");
        if (cJSON_IsString(who) && who->valuestring) {
            j.who = who->valuestring;
            j.has_who = true;
        } else if (!cJSON_IsNull(who)) {
            return fail(error, at + "'who' is neither a string nor null");
        }

        const cJSON* pure = item(e, "pure");
        if (!pure || !cJSON_IsBool(pure)) return fail(error, at + "no boolean 'pure'");
        j.pure = cJSON_IsTrue(pure);

        const cJSON* slice = item(e, "slice");
        if (!slice || !cJSON_IsString(slice) || !slice->valuestring)
            return fail(error, at + "no string 'slice'");
        j.slice = slice->valuestring;
        if (j.slice != "undo" && j.slice != "view" && j.slice != "host")
            return fail(error, at + "unknown slice '" + j.slice + "'");

        const cJSON* minted = item(e, "minted");
        if (!minted || !cJSON_IsArray(const_cast<cJSON*>(minted)))
            return fail(error, at + "no array 'minted'");
        for (const cJSON* m = minted->child; m; m = m->next) {
            if (!cJSON_IsString(m) || !m->valuestring)
                return fail(error, at + "'minted' holds a non-string");
            j.minted.push_back(m->valuestring);
        }

        out.push_back(j);
    }
    return true;
}

bool parse_journal(const std::string& json_text, std::vector<JournalEntry>& out,
                   std::string* error) {
    cJSON* root = cJSON_Parse(json_text.c_str());
    if (!root) {
        out.clear();
        return fail(error, "journal export is not JSON");
    }
    bool ok = parse_journal(root, out, error);
    cJSON_Delete(root);
    return ok;
}

}  // namespace voidpalabra
