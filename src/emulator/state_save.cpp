// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Writing a saved state (.ecats / .ecats.zip), source

#include "state_save.h"

#include <algorithm>

#include "config_ext.h"
#include "core.h"
#include "utils.h"

#include "dsk_tools/dsk_tools.h"
#include "libs/zip_writer.h"

namespace {

emulator::Result save_error(const char * message, const std::string &detail)
{
    return emulator::Result::error(emulator::ErrorCode::FileError,
        "{MachineState|" + std::string(message) + "} " + detail);
}

//Names inside the archive stay ASCII: ZipReader ignores the UTF-8 flag, and a
//name that survives every unpacker is worth more than the original spelling
std::string safe_member_name(const std::string &name)
{
    std::string r;
    for (size_t i = 0; i < name.size(); i++)
    {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
            || c == '.' || c == '-' || c == '_')
            r += static_cast<char>(c);
        else if (c == ' ')
            r += '_';
    }
    //Nothing usable was left, or it was all extension
    if (r.empty() || r[0] == '.') r = "file" + r;
    return r;
}

//Which folder of the bundle a dependency belongs in, by what it looks like.
//Only cosmetic: the reader resolves whatever relative name it is given
std::string folder_for(const std::string &name)
{
    //get_file_ext() keeps the dot and lowercases the rest
    const std::string ext = dsk_tools::get_file_ext(name);
    if (ext == ".rom" || ext == ".bin" || ext == ".hex") return "rom/";
    if (ext == ".map" || ext == ".keys" || ext == ".svg" || ext == ".png") return "keyboard/";
    if (ext == ".chr") return "data/";
    return "files/";
}

//A parameter value that names a file the machine loads. The scan is the
//primary source on purpose: it needs no code per device and so cannot quietly
//miss one, and a file copied in for nothing costs only bytes
bool looks_like_a_file(const EmulatorConfigDevice * d, const EmulatorConfigParameter &p,
                       EmulatorConfig * config, SystemData * sd, std::string &resolved)
{
    (void)d;
    //A connection or a list of ranges, never a file name
    if (p.name.empty() || p.name[0] == '~' || p.name[0] == '@') return false;
    if (p.value.empty()) return false;
    const size_t dot = p.value.find_last_of('.');
    if (dot == std::string::npos || dot + 1 == p.value.size()) return false;

    //"dev.iface" is a reference to another device, not a file
    const std::string head = p.value.substr(0, dot);
    if (head.find_first_of("/\\") == std::string::npos && config->get_device(head) != nullptr)
        return false;

    resolved = find_file_location(sd, p.value);
    return !resolved.empty();
}

} // namespace

//--------------------------------- Bundle ----------------------------------//

std::string StateBundle::unique_name(const std::string &wanted)
{
    bool taken = false;
    for (size_t i = 0; i < m_members.size() && !taken; i++)
        if (m_members[i].name == wanted) taken = true;
    if (!taken) return wanted;

    //Two different files of the same name from different directories
    const size_t dot = wanted.find_last_of('.');
    const std::string base = (dot == std::string::npos) ? wanted : wanted.substr(0, dot);
    const std::string ext  = (dot == std::string::npos) ? std::string() : wanted.substr(dot);
    for (unsigned int n = 2; n < 1000; n++)
    {
        const std::string candidate = base + "-" + std::to_string(n) + ext;
        bool used = false;
        for (size_t i = 0; i < m_members.size() && !used; i++)
            if (m_members[i].name == candidate) used = true;
        if (!used) return candidate;
    }
    return wanted;
}

std::string StateBundle::add_file(const std::string &path, const std::string &folder)
{
    for (size_t i = 0; i < m_by_source.size(); i++)
        if (m_by_source[i].first == path) return m_by_source[i].second;

    const std::string data = dsk_tools::utf8_read_file(path);
    if (data.empty() && dsk_tools::utf8_file_size(path) != 0)
    {
        if (m_error.empty()) m_error = "cannot read " + path;
        return std::string();
    }

    Member m;
    m.name = unique_name(m_prefix + folder + safe_member_name(dsk_tools::get_filename(path)));
    m.data.assign(data.begin(), data.end());
    m_members.push_back(m);
    m_by_source.push_back(std::make_pair(path, m.name));
    return m.name;
}

std::string StateBundle::put(const std::string &suggested, const uint8_t * data, size_t size)
{
    Member m;
    m.name = unique_name(m_prefix + "media/" + safe_member_name(suggested));
    if (size > 0) m.data.assign(data, data + size);
    m_members.push_back(m);
    return m.name;
}

//------------------------------- Writing -----------------------------------//

emulator::Result write_state_file(const StateSaveRequest &request)
{
    const bool packed = str_tolower(request.file_name).size() >= 4
        && str_tolower(request.file_name).compare(str_tolower(request.file_name).size() - 4, 4, ".zip") == 0;

    //Where inside the bundle the members go, see StateBundle::set_prefix()
    const std::string stem = machine_file_stem(request.file_name);
    const std::string base = dsk_tools::get_filename(stem);

    StateBundle bundle;
    if (!packed) bundle.set_prefix(base + ".files/");

    //-------- The configuration, with every file it names taken along -------//
    //Written from a copy, so that the running machine keeps pointing at the
    //files it actually loaded
    EmulatorConfig copy;
    emulator::Result res = copy.load_from_text(serialize_config(*request.config));
    if (!res) return res;

    for (unsigned int i = 0; i < copy.get_devices_count(); i++)
    {
        EmulatorConfigDevice * d = copy.get_device(static_cast<int>(i));
        ComputerDevice * live = (d->name == "system")
            ? nullptr : request.dm->get_device_by_name(d->name, false);

        for (size_t k = d->parameters.size(); k-- > 0; )
        {
            EmulatorConfigParameter &p = d->parameters[k];

            //A medium is state, not configuration: the device writes what is
            //in its drive now, which includes everything the machine has
            //written since the image was inserted. The parameter goes away,
            //so that loading the state does not send the device back to a
            //file on disk that no longer matches
            if (live != nullptr && live->state_owns_file(p.name))
            {
                d->parameters.erase(d->parameters.begin() + static_cast<std::ptrdiff_t>(k));
                continue;
            }

            std::string resolved;
            if (!looks_like_a_file(d, p, &copy, request.sd, resolved)) continue;
            const std::string name = bundle.add_file(resolved, folder_for(p.value));
            if (name.empty()) return save_error(QT_TRANSLATE_NOOP("MachineState", "Error reading file"), resolved);
            p.value = name;
            //Inline data of the configuration this came from is now an
            //ordinary member of the archive, which reads better
            p.right_extended.clear();
        }
    }

    //--------------------------- The state itself ---------------------------//
    StateWriter w(request.sd->radix, &bundle);

    w.begin_device("system", "");
    w.n64("clock", request.clock);
    w.n("radix", request.sd->radix);
    for (size_t i = 0; i < request.domains.size(); i++)
        w.n64(("domain[" + std::to_string(i) + "]").c_str(), request.domains[i]);
    w.end_device();

    for (unsigned int i = 0; i < request.dm->device_count; i++)
    {
        ComputerDevice * d = request.dm->get_device(i)->device.get();
        w.begin_device(d->name, d->type);

        //What the user changed through the Devices menu belongs to the
        //machine as the snapshot found it, and wins over the ini on the way
        //back in
        const DeviceOptions options = d->get_device_options();
        for (size_t k = 0; k < options.size(); k++)
            w.n_at("option", options[k].id, options[k].current);

        d->save_state(w);
        w.end_device();
    }

    if (!bundle.error().empty())
        return save_error(QT_TRANSLATE_NOOP("MachineState", "Error reading file"), bundle.error());

    MachineStateFile header;
    header.version = request.version;
    header.machine = request.machine;
    header.system_type = request.sd->system_type;
    header.system_name = request.sd->system_name;
    header.saved = timestamp_string();

    const std::string text = header.header()
        + "\n@config\n" + serialize_config(copy)
        + "\n@state " + std::to_string(ECATS_STATE_VERSION) + "\n\n" + w.text();

    //------------------------------ Out it goes -----------------------------//
    if (packed)
    {
        ZipWriter zip;
        zip.add(base + ".ecats", text);
        const std::vector<StateBundle::Member> &members = bundle.members();
        for (size_t i = 0; i < members.size(); i++)
            zip.add(members[i].name, members[i].data.data(), members[i].data.size());

        std::string archive;
        if (!zip.build(archive))
            return save_error(QT_TRANSLATE_NOOP("MachineState", "Error writing file"), request.file_name + ": " + zip.error());

        dsk_tools::UTF8_ofstream f(request.file_name, std::ios::binary);
        if (!f.good()) return save_error(QT_TRANSLATE_NOOP("MachineState", "Error writing file"), request.file_name);
        f.write(archive.data(), static_cast<std::streamsize>(archive.size()));
        f.close();
        return emulator::Result::ok();
    }

    //Unpacked: the text, and a directory of its own beside it holding what it
    //needs - which is what makes a state editable without repacking it every
    //time. The member names already carry that directory, so they are written
    //next to the text and resolve from there
    const std::string dir = dsk_tools::get_file_path(request.file_name);
    {
        dsk_tools::UTF8_ofstream f(request.file_name, std::ios::binary);
        if (!f.good()) return save_error(QT_TRANSLATE_NOOP("MachineState", "Error writing file"), request.file_name);
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        f.close();
    }
    const std::vector<StateBundle::Member> &members = bundle.members();
    for (size_t i = 0; i < members.size(); i++)
    {
        const std::string out = dir + members[i].name;
        const std::string folder = dsk_tools::get_file_path(out);
        for (size_t k = 1; k <= folder.size(); k++)
            if (k == folder.size() || folder[k] == '/' || folder[k] == '\\')
            {
                const std::string part = folder.substr(0, k);
                if (part.size() == 2 && part[1] == ':') continue;
                dsk_tools::utf8_mkdir(part);
            }
        dsk_tools::UTF8_ofstream f(out, std::ios::binary);
        if (!f.good()) return save_error(QT_TRANSLATE_NOOP("MachineState", "Error writing file"), out);
        if (!members[i].data.empty())
            f.write(reinterpret_cast<const char *>(members[i].data.data()),
                    static_cast<std::streamsize>(members[i].data.size()));
        f.close();
    }
    return emulator::Result::ok();
}
