#include "offsets.h"

namespace offsets
{

    std::uintptr_t base = 0;
    std::uintptr_t UWorldPtr = 0;
    std::uintptr_t GNames = 0;
    std::uintptr_t GObjects = 0;

    namespace Functions
    {
        std::uintptr_t ProcessEvent = 0;
        std::uintptr_t StaticFindObject = 0;
        std::uintptr_t FreeObjectName = 0;
    } // namespace Functions

    namespace
    {
        bool is_readable(DWORD protect)
        {
            if ((protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
                return false;
            switch (protect & 0xFF)
            {
            case PAGE_READONLY:
            case PAGE_READWRITE:
            case PAGE_WRITECOPY:
            case PAGE_EXECUTE:
            case PAGE_EXECUTE_READ:
            case PAGE_EXECUTE_READWRITE:
            case PAGE_EXECUTE_WRITECOPY:
                return true;
            default:
                return false;
            }
        }

        int hex_value(char value)
        {
            if (value >= '0' && value <= '9')
                return value - '0';
            if (value >= 'a' && value <= 'f')
                return value - 'a' + 10;
            if (value >= 'A' && value <= 'F')
                return value - 'A' + 10;
            return -1;
        }

        bool parse_signature(const char* signature, std::vector<int>& pattern)
        {
            if (!signature)
                return false;
            for (const char* cursor = signature; *cursor;)
            {
                if (*cursor == ' ')
                {
                    ++cursor;
                    continue;
                }
                if (*cursor == '?')
                {
                    pattern.push_back(-1);
                    ++cursor;
                    if (*cursor == '?')
                        ++cursor;
                    continue;
                }
                const int high = hex_value(*cursor++);
                const int low = hex_value(*cursor++);
                if (high < 0 || low < 0)
                    return false;
                pattern.push_back((high << 4) | low);
            }
            return !pattern.empty();
        }

        bool matches_at(const std::vector<std::uint8_t>& bytes, std::size_t offset, const std::vector<int>& pattern)
        {
            for (std::size_t index = 0; index < pattern.size(); ++index)
                if (pattern[index] != -1 && bytes[offset + index] != static_cast<std::uint8_t>(pattern[index]))
                    return false;
            return true;
        }

        std::uintptr_t scan_executable_range(std::uintptr_t start, std::uintptr_t end, const std::vector<int>& pattern)
        {
            std::vector<std::uint8_t> carry;
            const std::size_t carry_size = pattern.size() > 1 ? pattern.size() - 1 : 0;
            for (std::uintptr_t cursor = start; cursor < end;)
            {
                MEMORY_BASIC_INFORMATION memory = {};
                if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &memory, sizeof(memory)))
                    return 0;
                const auto region_start = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
                const auto region_end = region_start + memory.RegionSize;
                if (region_end <= cursor)
                    return 0;
                const std::uintptr_t range_start = (std::max)(cursor, region_start);
                const std::uintptr_t range_end = (std::min)(end, region_end);
                if (memory.State != MEM_COMMIT || !is_readable(memory.Protect))
                {
                    carry.clear();
                    cursor = range_end;
                    continue;
                }

                for (std::uintptr_t page = range_start; page < range_end;)
                {
                    const std::size_t bytes_to_read = static_cast<std::size_t>((std::min)(range_end - page, static_cast<std::uintptr_t>(0x1000)));
                    std::vector<std::uint8_t> page_bytes(bytes_to_read);
                    if (!read_mem(page, page_bytes.data(), bytes_to_read))
                    {
                        carry.clear();
                        page += bytes_to_read;
                        continue;
                    }

                    const std::size_t carried = carry.size();
                    std::vector<std::uint8_t> window = carry;
                    window.insert(window.end(), page_bytes.begin(), page_bytes.end());
                    for (std::size_t index = 0; index + pattern.size() <= window.size(); ++index)
                        if (matches_at(window, index, pattern))
                            return page - carried + index;
                    carry.assign(window.end() - (std::min)(carry_size, window.size()), window.end());
                    page += bytes_to_read;
                }
                cursor = range_end;
            }
            return 0;
        }

        std::uintptr_t pattern_scan_impl(std::uintptr_t module_base, const char* signature)
        {
            IMAGE_DOS_HEADER dos = {};
            if (!read_mem(module_base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000)
                return 0;

            IMAGE_NT_HEADERS64 nt = {};
            const auto nt_address = module_base + static_cast<std::uint32_t>(dos.e_lfanew);
            if (nt_address < module_base || !read_mem(nt_address, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.OptionalHeader.SizeOfImage == 0 || nt.FileHeader.NumberOfSections == 0)
                return 0;

            std::vector<int> pattern;
            if (!parse_signature(signature, pattern))
                return 0;
            const std::uintptr_t image_end = module_base + nt.OptionalHeader.SizeOfImage;
            if (image_end <= module_base)
                return 0;
            const std::uintptr_t section_table = nt_address + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;

            for (WORD index = 0; index < nt.FileHeader.NumberOfSections; ++index)
            {
                IMAGE_SECTION_HEADER section = {};
                const std::uintptr_t section_address = section_table + static_cast<std::uintptr_t>(index) * sizeof(section);
                if (section_address < section_table || !read_mem(section_address, &section, sizeof(section)))
                    return 0;
                if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                    continue;
                const std::size_t virtual_size = section.Misc.VirtualSize ? section.Misc.VirtualSize : section.SizeOfRawData;
                const std::uintptr_t section_start = module_base + section.VirtualAddress;
                const std::uintptr_t section_end = section_start + virtual_size;
                if (section_start < module_base || section_end <= section_start)
                    continue;
                const auto match = scan_executable_range(section_start, (std::min)(section_end, image_end), pattern);
                if (match)
                    return match;
            }
            return 0;
        }

        std::uintptr_t resolve_rip_impl(std::uintptr_t address, int displacement_offset, int instruction_size)
        {
            if (!address || displacement_offset < 0 || instruction_size <= 0)
                return 0;
            const std::uintptr_t displacement_address =
                address + static_cast<std::uintptr_t>(displacement_offset);
            if (displacement_address < address)
                return 0;
            std::int32_t displacement = 0;
            if (!read_mem(displacement_address, &displacement, sizeof(displacement)))
                return 0;
            return address + static_cast<std::uintptr_t>(instruction_size) + displacement;
        }
    } // namespace

    std::uintptr_t pattern_scan(std::uintptr_t module_base, const char* signature)
    {
        return pattern_scan_impl(module_base, signature);
    }

    std::uintptr_t resolve_rip(std::uintptr_t address, int displacement_offset, int instruction_size)
    {
        return resolve_rip_impl(address, displacement_offset, instruction_size);
    }

    void setup()
    {
        base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));

        auto gworld_addr = pattern_scan(base, "48 89 1D ? ? ? ? 48 85 DB 74 ? 41 83 BF");
        if (gworld_addr)
        {
            UWorldPtr = resolve_rip(gworld_addr, 3, 7) - base;
            log("offsets: GWorld = 0x%llX (RVA)", UWorldPtr);
        }
        else
        {
            UWorldPtr = 0xD095198;
            log("offsets: GWorld pattern failed, using hardcoded 0x%llX", UWorldPtr);
        }

        auto gnames_addr = pattern_scan(base, "48 8D 0D ? ? ? ? E8 ? ? ? ? 4C 8B C0 C6 05 ? ? ? ? ? 8B D3 0F B7 C3 C1 EA ? 89 54 24 ? 89 44 24 ? 48 8B 4C 24 ? 49 8B 5C D0");
        if (gnames_addr)
        {
            GNames = resolve_rip(gnames_addr, 3, 7) - base;
            log("offsets: GNames = 0x%llX (RVA)", GNames);
        }
        else
        {
            GNames = 0xCE36240;
            log("offsets: GNames pattern failed, using hardcoded 0x%llX", GNames);
        }

        auto gobjects_addr = pattern_scan(base, "48 8B 0D ? ? ? ? 48 8B 14 D1 4A 8D 0C C2 EB ? 33 C9 48 8B 09 48 C1 F9 ? 0F BA E1 ? 72 ? 48 8B 40 ? EB ? 48 8B 48 ? 48 8D 54 24 ? 48 89 4C 24 ? 48 8D 4C 24 ? E8 ? ? ? ? EB ? 48 8D 15 ? ? ? ? 48 8D 4C 24 ? E8 ? ? ? ? 83 7C 24 ? ? 48 8D 15");
        if (gobjects_addr)
        {
            GObjects = resolve_rip(gobjects_addr, 3, 7) - base;
            log("offsets: GObjects = 0x%llX (RVA)", GObjects);
        }
        else
        {
            GObjects = 0xCF08A60;
            log("offsets: GObjects pattern failed, using hardcoded 0x%llX", GObjects);
        }

        auto staticfind_addr = pattern_scan(base, "4C 8B DC 53 55 56 57 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 ? ? ? ? 49 8B 78");
        if (staticfind_addr)
        {
            Functions::StaticFindObject = staticfind_addr - base;
            log("offsets: StaticFindObject = 0x%llX (RVA)", Functions::StaticFindObject);
        }
        else
        {
            Functions::StaticFindObject = 0x18846E0;
            log("offsets: StaticFindObject pattern failed, using hardcoded 0x%llX", Functions::StaticFindObject);
        }

        auto processevent_addr = pattern_scan(base, "40 55 56 57 41 54 41 55 41 56 41 57 48 81 EC ? ? ? ? 48 8D 6C 24 ? 48 89 9D ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C5 48 89 85 ? ? ? ? 8B 41");
        if (processevent_addr)
        {
            Functions::ProcessEvent = processevent_addr - base;
            log("offsets: ProcessEvent = 0x%llX (RVA)", Functions::ProcessEvent);
        }
        else
        {
            Functions::ProcessEvent = 0x1850CE0;
            log("offsets: ProcessEvent pattern failed, using hardcoded 0x%llX", Functions::ProcessEvent);
        }

        auto freeobjectname_addr = pattern_scan(base, "48 85 C9 74 ? 53 48 83 EC ? 48 8B D9 48 8B 0D");
        if (freeobjectname_addr)
        {
            Functions::FreeObjectName = freeobjectname_addr - base;
            log("offsets: FreeObjectName = 0x%llX (RVA)", Functions::FreeObjectName);
        }
        else
        {
            Functions::FreeObjectName = 0x1538E90;
            log("offsets: FreeObjectName pattern failed, using hardcoded 0x%llX", Functions::FreeObjectName);
        }
    }
} // namespace offsets
