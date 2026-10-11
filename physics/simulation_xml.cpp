#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "simulation_xml.h"

#include <windows.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <xmllite.h>

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <locale.h>
#include <map>
#include <set>
#include <stdexcept>

#pragma comment(lib, "xmllite.lib")
#pragma comment(lib, "shlwapi.lib")

namespace
{
    using Microsoft::WRL::ComPtr;

    std::string narrow(const wchar_t* text)
    {
        const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
        if (length <= 1)
            return {};
        std::string result(static_cast<size_t>(length - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text, -1, &result[0], length, nullptr, nullptr);
        return result;
    }

    // Attributes of one element; every attribute must be consumed or the element is rejected.
    class attributes
    {
    public:
        attributes(std::string element, unsigned line, std::map<std::string, std::string> values)
            : element_(std::move(element)), line_(line), values_(std::move(values))
        {
        }

        [[noreturn]] void fail(const std::string& message) const
        {
            throw std::runtime_error("line " + std::to_string(line_) + ": <" + element_ + "> " + message);
        }

        double number(const char* name)
        {
            return parse_number(name, require(name));
        }

        double number_or(const char* name, double fallback)
        {
            const auto it = values_.find(name);
            if (it == values_.end())
                return fallback;
            used_.insert(name);
            return parse_number(name, it->second);
        }

        size_t index(const char* name)
        {
            const auto& text = require(name);
            char* end = nullptr;
            const auto value = std::strtoull(text.c_str(), &end, 10);
            if (text.empty() || text[0] == '-' || *end != '\0')
                fail(std::string("attribute '") + name + "' must be a non-negative integer, got '" + text + "'");
            return static_cast<size_t>(value);
        }

        bool flag_or(const char* name, bool fallback)
        {
            const auto it = values_.find(name);
            if (it == values_.end())
                return fallback;
            used_.insert(name);
            if (it->second == "true" || it->second == "1")
                return true;
            if (it->second == "false" || it->second == "0")
                return false;
            fail(std::string("attribute '") + name + "' must be true or false, got '" + it->second + "'");
        }

        // "#RRGGBB", for display only.
        std::optional<uint32_t> colour()
        {
            const auto it = values_.find("colour");
            if (it == values_.end())
                return std::nullopt;
            used_.insert("colour");

            const auto& text = it->second;
            bool valid = text.size() == 7 && text[0] == '#';
            for (size_t i = 1; valid && i < text.size(); ++i)
                valid = std::isxdigit(static_cast<unsigned char>(text[i])) != 0;
            if (!valid)
                fail("attribute 'colour' must look like #RRGGBB, got '" + text + "'");
            return static_cast<uint32_t>(std::strtoul(text.c_str() + 1, nullptr, 16));
        }

        // Free text; an empty value is kept (it means "no label").
        std::optional<std::string> text(const char* name)
        {
            const auto it = values_.find(name);
            if (it == values_.end())
                return std::nullopt;
            used_.insert(name);
            return it->second;
        }

        void finish() const
        {
            for (const auto& entry : values_)
            {
                if (!used_.count(entry.first))
                    fail("has unknown attribute '" + entry.first + "'");
            }
        }

    private:
        const std::string& require(const char* name)
        {
            const auto it = values_.find(name);
            if (it == values_.end())
                fail(std::string("is missing attribute '") + name + "'");
            used_.insert(name);
            return it->second;
        }

        double parse_number(const char* name, const std::string& text) const
        {
            // C locale and correct rounding so values written by to_chars round-trip exactly.
            static const _locale_t c_locale = _create_locale(LC_NUMERIC, "C");
            char* end = nullptr;
            const double value = _strtod_l(text.c_str(), &end, c_locale);
            if (text.empty() || *end != '\0' || !std::isfinite(value))
                fail(std::string("attribute '") + name + "' must be a finite number, got '" + text + "'");
            return value;
        }

        std::string element_;
        unsigned line_;
        std::map<std::string, std::string> values_;
        std::set<std::string> used_;
    };

    struct parsed
    {
        bool has_simulation = false;
        double xmin = 0, xmax = 0, ymin = 0, ymax = 0, dt = 0, min_gravitational_mass = 10000.0;
        wall_flags walls;
        double speed = 0.0;   // 0 = not given
        double softening = 0.0;
        size_t iterations = 0;
        std::vector<mass> masses;
        std::vector<mass_state> mass_states;
        std::vector<spring> springs;
        std::vector<spring_state> spring_states;
        std::vector<damper> dampers;
        std::vector<damper_state> damper_states;
    };

    void read_simulation(IXmlReader& reader, parsed& out)
    {
        std::string section;
        std::set<std::string> seen_sections;
        std::vector<std::pair<size_t, size_t>> spring_refs, damper_refs;
        std::vector<unsigned> spring_lines, damper_lines;

        XmlNodeType node_type;
        HRESULT hr;
        while ((hr = reader.Read(&node_type)) == S_OK)
        {
            if (node_type != XmlNodeType_Element)
                continue;

            UINT depth = 0;
            reader.GetDepth(&depth);
            UINT line = 0;
            reader.GetLineNumber(&line);

            const wchar_t* local_name = nullptr;
            reader.GetLocalName(&local_name, nullptr);
            const std::string name = narrow(local_name);

            std::map<std::string, std::string> values;
            if (reader.MoveToFirstAttribute() == S_OK)
            {
                do
                {
                    const wchar_t* attribute_name = nullptr;
                    const wchar_t* attribute_value = nullptr;
                    reader.GetLocalName(&attribute_name, nullptr);
                    reader.GetValue(&attribute_value, nullptr);
                    values[narrow(attribute_name)] = narrow(attribute_value);
                } while (reader.MoveToNextAttribute() == S_OK);
            }
            attributes attrs(name, line, std::move(values));

            if (depth == 0)
            {
                if (name != "simulation")
                    attrs.fail("is not the expected root element <simulation>");
                out.xmin = attrs.number("xmin");
                out.xmax = attrs.number("xmax");
                out.ymin = attrs.number("ymin");
                out.ymax = attrs.number("ymax");
                out.dt = attrs.number("dt");
                const auto iterations = attrs.number("iterations");
                out.min_gravitational_mass = attrs.number_or("min_gravitational_mass", 10000.0);
                out.walls.left = attrs.flag_or("bounce_left", false);
                out.walls.right = attrs.flag_or("bounce_right", false);
                out.walls.top = attrs.flag_or("bounce_top", false);
                out.walls.bottom = attrs.flag_or("bounce_bottom", true);
                out.speed = attrs.number_or("speed", 0.0);
                if (out.speed < 0.0)
                    attrs.fail("needs speed > 0");
                out.softening = attrs.number_or("softening", 0.0);
                if (out.softening < 0.0)
                    attrs.fail("needs softening >= 0");
                attrs.finish();

                if (!(out.xmax > out.xmin) || !(out.ymax > out.ymin))
                    attrs.fail("needs xmax > xmin and ymax > ymin");
                if (!(out.dt > 0))
                    attrs.fail("needs dt > 0");
                if (iterations < 1 || iterations != std::floor(iterations))
                    attrs.fail("needs iterations to be a positive integer");
                out.iterations = static_cast<size_t>(iterations);
                out.has_simulation = true;
            }
            else if (depth == 1)
            {
                if (name != "masses" && name != "springs" && name != "dampers")
                    attrs.fail("is not allowed here (expected <masses>, <springs> or <dampers>)");
                if (!seen_sections.insert(name).second)
                    attrs.fail("appears more than once");
                attrs.finish();
                section = name;
            }
            else if (depth == 2)
            {
                if (name == "mass" && section == "masses")
                {
                    const auto m = attrs.number("m");
                    const auto r = attrs.number("r");
                    const auto x = attrs.number("x");
                    const auto y = attrs.number("y");
                    const auto e = attrs.number_or("e", 1.0);
                    const auto vx = attrs.number_or("vx", 0.0);
                    const auto vy = attrs.number_or("vy", 0.0);
                    const auto fixed = attrs.flag_or("fixed", false);
                    const auto colour = attrs.colour();
                    const auto label = attrs.text("label");
                    attrs.finish();

                    if (!(m > 0))
                        attrs.fail("needs m > 0");
                    if (!(r >= 0))
                        attrs.fail("needs r >= 0");

                    out.masses.emplace_back(m, r, e, colour, label);
                    out.mass_states.emplace_back(vector2(x, y), vector2{}, vector2(vx, vy), vector2{}, 0.0, 0.0, fixed);
                }
                else if (name == "spring" && section == "springs")
                {
                    const auto a = attrs.index("a");
                    const auto b = attrs.index("b");
                    const auto k = attrs.number("k");
                    const auto l0 = attrs.number("l0");
                    const auto min_factor = attrs.number_or("min", 0.5);
                    const auto max_factor = attrs.number_or("max", 1.5);
                    const auto colour = attrs.colour();
                    attrs.finish();

                    out.springs.emplace_back(a, b, k, l0, min_factor, max_factor, colour);
                    out.spring_states.emplace_back(false);
                    spring_refs.emplace_back(a, b);
                    spring_lines.push_back(line);
                }
                else if (name == "damper" && section == "dampers")
                {
                    const auto a = attrs.index("a");
                    const auto b = attrs.index("b");
                    const auto k = attrs.number("k");
                    const auto l0 = attrs.number_or("l0", 0.0);
                    attrs.finish();

                    out.dampers.emplace_back(a, b, k, l0);
                    out.damper_states.emplace_back(false);
                    damper_refs.emplace_back(a, b);
                    damper_lines.push_back(line);
                }
                else
                {
                    attrs.fail("is not allowed inside <" + section + ">");
                }
            }
            else
            {
                attrs.fail("is nested too deeply");
            }
        }

        if (FAILED(hr))
        {
            UINT line = 0;
            reader.GetLineNumber(&line);
            char code[32];
            snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(hr));
            throw std::runtime_error("line " + std::to_string(line) + ": malformed XML (HRESULT " + code + ")");
        }
        if (!out.has_simulation)
            throw std::runtime_error("no <simulation> element found");

        auto check_refs = [&](const std::vector<std::pair<size_t, size_t>>& refs, const std::vector<unsigned>& lines, const char* what) {
            for (size_t i = 0; i < refs.size(); ++i)
            {
                if (refs[i].first >= out.masses.size() || refs[i].second >= out.masses.size())
                    throw std::runtime_error("line " + std::to_string(lines[i]) + ": <" + what + "> refers to a mass index outside 0.." + std::to_string(out.masses.size()) + " (" + std::to_string(refs[i].first) + ", " + std::to_string(refs[i].second) + ")");
                if (refs[i].first == refs[i].second)
                    throw std::runtime_error("line " + std::to_string(lines[i]) + ": <" + what + "> connects a mass to itself");
            }
        };
        check_refs(spring_refs, spring_lines, "spring");
        check_refs(damper_refs, damper_lines, "damper");
    }
}

namespace
{
    std::shared_ptr<simulation> load_from_stream(IStream* stream, std::vector<mass_state>& masses, std::vector<spring_state>& springs, std::vector<damper_state>& dampers)
    {
        ComPtr<IXmlReader> reader;
        if (FAILED(CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(reader.GetAddressOf()), nullptr)))
            throw std::runtime_error("cannot create XML reader");
        reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit);
        if (FAILED(reader->SetInput(stream)))
            throw std::runtime_error("cannot read XML input");

        parsed data;
        read_simulation(*reader.Get(), data);

        masses = std::move(data.mass_states);
        springs = std::move(data.spring_states);
        dampers = std::move(data.damper_states);
        return std::make_shared<simulation>(model_system(data.masses, data.springs, data.dampers), data.xmin, data.xmax, data.ymin, data.ymax, data.dt, data.iterations, data.min_gravitational_mass, data.walls, data.speed, data.softening);
    }

    std::string number_text(double value)
    {
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);   // shortest text that round-trips exactly
        return std::string(buffer, result.ptr);
    }

    std::string xml_escaped(const std::string& text)
    {
        std::string out;
        for (const char c : text)
        {
            switch (c)
            {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += (static_cast<unsigned char>(c) < 0x20) ? ' ' : c;
            }
        }
        return out;
    }

    std::string colour_text(uint32_t rgb)
    {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "#%06X", static_cast<unsigned>(rgb & 0xFFFFFF));
        return buffer;
    }
}

std::shared_ptr<simulation> load_simulation_xml(const std::wstring& path, std::vector<mass_state>& masses, std::vector<spring_state>& springs, std::vector<damper_state>& dampers, std::string& error)
{
    try
    {
        ComPtr<IStream> stream;
        if (FAILED(SHCreateStreamOnFileW(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, &stream)))
            throw std::runtime_error("cannot open file");
        return load_from_stream(stream.Get(), masses, springs, dampers);
    }
    catch (const std::exception& ex)
    {
        error = narrow(path.c_str()) + ": " + ex.what();
        return nullptr;
    }
}

std::shared_ptr<simulation> load_simulation_xml_text(const std::string& xml, std::vector<mass_state>& masses, std::vector<spring_state>& springs, std::vector<damper_state>& dampers, std::string& error)
{
    try
    {
        ComPtr<IStream> stream;
        stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(xml.data()), static_cast<UINT>(xml.size())));
        if (!stream)
            throw std::runtime_error("cannot read XML text");
        return load_from_stream(stream.Get(), masses, springs, dampers);
    }
    catch (const std::exception& ex)
    {
        error = ex.what();
        return nullptr;
    }
}

std::string simulation_to_xml(const simulation& sim, const std::vector<mass_state>& masses)
{
    std::string out;
    out += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    out += "<simulation xmin=\"" + number_text(sim.xmin()) + "\" xmax=\"" + number_text(sim.xmax()) +
        "\" ymin=\"" + number_text(sim.ymin()) + "\" ymax=\"" + number_text(sim.ymax()) +
        "\" dt=\"" + number_text(sim.dt()) + "\" iterations=\"" + std::to_string(sim.iterations_per_update()) + "\"";
    if (sim.min_gravitational_mass() != 10000.0)
        out += " min_gravitational_mass=\"" + number_text(sim.min_gravitational_mass()) + "\"";
    const auto& walls = sim.walls();
    if (walls.left)
        out += " bounce_left=\"true\"";
    if (walls.right)
        out += " bounce_right=\"true\"";
    if (walls.top)
        out += " bounce_top=\"true\"";
    if (!walls.bottom)
        out += " bounce_bottom=\"false\"";
    if (sim.speed() != sim.dt() * double(sim.iterations_per_update()) * 60.0)
        out += " speed=\"" + number_text(sim.speed()) + "\"";
    if (sim.softening() != 0.0)
        out += " softening=\"" + number_text(sim.softening()) + "\"";
    out += ">\n";

    const auto& model = sim.model();

    out += "  <masses>\n";
    for (size_t i = 0; i < model.masses().size(); ++i)
    {
        const auto& m = model.masses()[i];
        const auto& s = masses[i];
        out += "    <mass m=\"" + number_text(m.m()) + "\" r=\"" + number_text(m.r()) + "\" x=\"" + number_text(s.position_.x()) + "\" y=\"" + number_text(s.position_.y()) + "\"";
        if (m.e() != 1.0)
            out += " e=\"" + number_text(m.e()) + "\"";
        if (s.velocity_.x() != 0.0)
            out += " vx=\"" + number_text(s.velocity_.x()) + "\"";
        if (s.velocity_.y() != 0.0)
            out += " vy=\"" + number_text(s.velocity_.y()) + "\"";
        if (s.fixed_)
            out += " fixed=\"true\"";
        if (m.colour())
            out += " colour=\"" + colour_text(*m.colour()) + "\"";
        if (m.label())
            out += " label=\"" + xml_escaped(*m.label()) + "\"";
        out += "/>\n";
    }
    out += "  </masses>\n";

    if (!model.springs().empty())
    {
        out += "  <springs>\n";
        for (const auto& s : model.springs())
        {
            out += "    <spring a=\"" + std::to_string(s.id_mass1()) + "\" b=\"" + std::to_string(s.id_mass2()) + "\" k=\"" + number_text(s.k()) + "\" l0=\"" + number_text(s.l0()) + "\"";
            if (s.min_factor() != 0.5)
                out += " min=\"" + number_text(s.min_factor()) + "\"";
            if (s.max_factor() != 1.5)
                out += " max=\"" + number_text(s.max_factor()) + "\"";
            if (s.colour())
                out += " colour=\"" + colour_text(*s.colour()) + "\"";
            out += "/>\n";
        }
        out += "  </springs>\n";
    }

    if (!model.dampers().empty())
    {
        out += "  <dampers>\n";
        for (const auto& d : model.dampers())
        {
            out += "    <damper a=\"" + std::to_string(d.id_mass1()) + "\" b=\"" + std::to_string(d.id_mass2()) + "\" k=\"" + number_text(d.k()) + "\"";
            if (d.l0() != 0.0)
                out += " l0=\"" + number_text(d.l0()) + "\"";
            out += "/>\n";
        }
        out += "  </dampers>\n";
    }

    out += "</simulation>\n";
    return out;
}

bool save_simulation_xml(const std::wstring& path, const simulation& sim, const std::vector<mass_state>& masses, std::string& error)
{
    std::ofstream file(path, std::ios::binary);
    if (!file)
    {
        error = narrow(path.c_str()) + ": cannot create file";
        return false;
    }
    file << simulation_to_xml(sim, masses);
    file.close();
    if (!file)
    {
        error = narrow(path.c_str()) + ": write failed";
        return false;
    }
    return true;
}
