// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Model of the configuration extension editor, source

#include "config_fields.h"
#include "base64.h"
#include "emulator.h"
#include "utils.h"

#include "dsk_tools/dsk_tools.h"
#include "libs/dsk_tools/src/utils.h"

namespace {

emulator::Result model_error(const char * message, const std::string &detail)
{
    return emulator::Result::error(emulator::ErrorCode::ConfigError,
        "{EmulatorConfig|" + std::string(message) + "} " + detail);
}

//The line of a field in a parsed configuration, if it has one
void read_line(EmulatorConfig &config, const std::string &device, const std::string &name,
               bool &present, std::string &value, std::string &extended)
{
    present = false;
    value.clear();
    extended.clear();
    EmulatorConfigDevice * d = config.get_device(device);
    if (d == nullptr) return;
    const std::vector<size_t> found = d->find_parameters(name);
    if (found.empty()) return;
    present = true;
    value = d->parameters[found[0]].value;
    extended = d->parameters[found[0]].right_extended;
}

bool ends_with_ci(const std::string &s, const std::string &suffix)
{
    return s.size() >= suffix.size() && str_tolower(s.substr(s.size() - suffix.size())) == suffix;
}

} // namespace

bool DeviceConfigField::is_inline() const
{
    return str_tolower(str_trim(extended).substr(0, 7)) == "base64:";
}

bool DeviceConfigField::changed() const
{
    return present != base_present || value != base_value || extended != base_extended;
}

emulator::Result collect_config_fields(EmulatorConfig &config, std::vector<DeviceConfigField> &out)
{
    out.clear();

    //The same factories the emulator uses, on managers of its own. The
    //devices are constructed only: nothing is opened, nothing is wired
    DeviceManager * dm = new DeviceManager();
    InterfaceManager * im = new InterfaceManager(dm);
    register_all_devices(dm);

    emulator::Result res = emulator::Result::ok();
    for (unsigned int i = 0; i < config.get_devices_count() && res; i++)
    {
        EmulatorConfigDevice * d = config.get_device(i);
        if (!d->type.empty()) res = dm->add_device(im, d);
    }
    if (res)
        for (unsigned int i = 0; i < dm->device_count; i++)
        {
            ComputerDevice * dev = dm->get_device(i)->device.get();
            const ConfigFields fields = dev->get_config_fields();
            for (size_t j = 0; j < fields.size(); j++)
            {
                DeviceConfigField f;
                f.device = dev->name;
                f.field = fields[j];
                out.push_back(f);
            }
        }

    //In the order the emulator tears a machine down
    delete dm;
    delete im;
    return res;
}

emulator::Result ExtEditModel::open(const std::string &path, const MachinePaths &paths)
{
    file.clear();
    ext = ConfigExtension();
    fields.clear();

    if (ends_with_ci(path, ".ext.zip"))
        return model_error(QT_TRANSLATE_NOOP("EmulatorConfig", "A packed extension cannot be edited"), path);

    if (is_extension_file(path))
    {
        const std::string text = dsk_tools::utf8_read_file(path);
        if (text.empty())
            return model_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Error reading config file"), path);
        emulator::Result res = ext.parse(text, path);
        if (!res) return res;
        file = path;
        extends = ext.extends;
        base_cfg = is_absolute_path(extends) ? extends : paths.computers_path + extends;
        version = ext.version;
    }
    else
    {
        base_cfg = path;
        //Relative to computers/ when the base is there, so the extension
        //works from wherever it is saved
        extends = path;
        const std::string &root = paths.computers_path;
        if (!root.empty() && path.compare(0, root.size(), root) == 0)
            extends = path.substr(root.size());
        for (size_t i = 0; i < extends.size(); i++) if (extends[i] == '\\') extends[i] = '/';
    }

    EmulatorConfig base;
    emulator::Result res = base.load_from_file(base_cfg);
    if (!res) return res;
    EmulatorConfigDevice * system = base.get_device("system");
    base_version = system != nullptr ? system->get_parameter("version", false).value : std::string();
    if (file.empty()) version = base_version;

    //The same base with the extension applied: the values in effect
    EmulatorConfig current;
    res = current.load_from_file(base_cfg);
    if (!res) return res;
    if (!file.empty())
    {
        res = ext.apply(current);
        if (!res) return res;
    }

    res = collect_config_fields(current, fields);
    if (!res) return res;
    for (size_t i = 0; i < fields.size(); i++)
    {
        DeviceConfigField &f = fields[i];
        read_line(base, f.device, f.field.name, f.base_present, f.base_value, f.base_extended);
        read_line(current, f.device, f.field.name, f.present, f.value, f.extended);
    }
    return emulator::Result::ok();
}

std::string ExtEditModel::build()
{
    ext.extends = extends;
    ext.version = version;

    for (size_t i = 0; i < fields.size(); i++)
    {
        const DeviceConfigField &f = fields[i];

        //Whatever the extension said about this field is replaced
        std::vector<ExtEdit> kept;
        for (size_t j = 0; j < ext.edits.size(); j++)
        {
            const ExtEdit &e = ext.edits[j];
            if (e.device == f.device && e.param.name == f.field.name && e.param.left_range.empty()) continue;
            kept.push_back(e);
        }
        ext.edits.swap(kept);

        if (!f.changed()) continue;

        ExtEdit e;
        e.device = f.device;
        e.param.name = f.field.name;
        if (!f.present)
        {
            //Nothing to remove if the base does not have it either
            if (!f.base_present) continue;
            e.op = ExtEdit::Remove;
        } else {
            e.op = ExtEdit::Set;
            e.param.value = f.value;
            e.param.right_extended = f.extended;
        }
        ext.edits.push_back(e);
    }
    return ext.serialize();
}

emulator::Result ExtEditModel::embed_file(DeviceConfigField &f, const std::string &path)
{
    if (!dsk_tools::file_exists(path))
        return model_error(QT_TRANSLATE_NOOP("EmulatorConfig", "Error reading config file"), path);
    const std::string data = dsk_tools::utf8_read_file(path);
    const std::vector<unsigned char> bytes(data.begin(), data.end());

    //76 characters to a line, indented under the property
    const std::string encoded = base64_encode(bytes);
    std::string block = "base64:\n";
    for (size_t start = 0; start < encoded.size(); start += 76)
        block += "    " + encoded.substr(start, 76) + "\n";

    f.present = true;
    f.value = dsk_tools::get_filename(path);
    f.extended = block;
    return emulator::Result::ok();
}

std::string unique_version(const std::string &name, const std::vector<std::string> &taken)
{
    std::string stem = str_trim(name);
    //A number of a previous copy at the end: "Name (2)"
    if (!stem.empty() && stem[stem.size() - 1] == ')')
    {
        const size_t open = stem.rfind(" (");
        if (open != std::string::npos)
        {
            const std::string digits = stem.substr(open + 2, stem.size() - open - 3);
            if (!digits.empty() && digits.find_first_not_of("0123456789") == std::string::npos)
                stem = stem.substr(0, open);
        }
    }
    for (unsigned n = 1; ; n++)
    {
        const std::string candidate = stem + " (" + std::to_string(n) + ")";
        bool used = false;
        for (size_t i = 0; i < taken.size() && !used; i++) used = taken[i] == candidate;
        if (!used) return candidate;
    }
}

std::string unique_ext_file(const std::string &dir, const std::string &stem)
{
    for (unsigned n = 1; ; n++)
    {
        const std::string candidate = dir + stem + "-" + std::to_string(n) + ".ext";
        if (!dsk_tools::file_exists(candidate)) return candidate;
    }
}
