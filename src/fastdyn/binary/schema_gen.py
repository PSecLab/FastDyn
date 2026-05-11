import os
from elftools.elf.elffile import ELFFile

# These match the FieldType enum in your C plugin exactly
FIELD_UINT32 = 0
FIELD_UINT16 = 1
FIELD_UINT8 = 2
FIELD_POINTER = 3
FIELD_STRING_INLINE = 4
FIELD_STRING_PTR = 5

class SchemaGenerator:
    def __init__(self, binary_path):
        self.binary_path = binary_path
        with open(binary_path, 'rb') as f:
            elf = ELFFile(f)
            if not elf.has_dwarf_info():
                raise ValueError("[!] Target binary does not contain DWARF debug symbols.")
            self.dwarf = elf.get_dwarf_info()

        # Cache DIEs by their offset for fast type lookups
        self.die_cache = self._build_die_cache()

    def _build_die_cache(self):
        """Indexes all DIEs to easily follow DW_AT_type references."""
        cache = {}
        for cu in self.dwarf.iter_CUs():
            for die in cu.iter_DIEs():
                cache[die.offset] = die
        return cache

    def generate_schema(self, target_structs, target_symbols):
            """Extracts the specified structs and returns the flat schema as a string."""
            parsed_structs = {}

            # 1. Hunt down the target structs
            for cu in self.dwarf.iter_CUs():
                for die in cu.iter_DIEs():
                    if die.tag == 'DW_TAG_structure_type' and 'DW_AT_name' in die.attributes:
                        struct_name = die.attributes['DW_AT_name'].value.decode('utf-8')

                        if struct_name in target_structs and struct_name not in parsed_structs:
                            # Flatten it!
                            parsed_structs[struct_name] = self._flatten_struct(die)

            # 2. Build the schema string line-by-line
            output_lines = []

            for struct_name, fields in parsed_structs.items():
                output_lines.append(f"STRUCT {struct_name} {len(fields)}")
                for field in fields:
                    output_lines.append(f"{field['name']} {field['offset']} {field['size']} {field['type']}")

            # 3. Add the Global Symbols
            for sym_name, sym_addr in target_symbols.items():
                # Write address in hex for easier debugging
                output_lines.append(f"SYMBOL {sym_name} {hex(sym_addr)}")

            # 4. Return as a single string joined by newlines (adding a trailing newline)
            return "\n".join(output_lines) + "\n"

    def _flatten_struct(self, struct_die, prefix="", base_offset=0):
        """Recursively flattens fields and calculates absolute offsets."""
        fields = []

        for child in struct_die.iter_children():
            if child.tag == 'DW_TAG_member':
                field_name = child.attributes['DW_AT_name'].value.decode('utf-8')
                full_name = f"{prefix}{field_name}"

                # Get relative offset and convert to absolute
                if 'DW_AT_data_member_location' in child.attributes:
                    rel_offset = child.attributes['DW_AT_data_member_location'].value
                else:
                    rel_offset = 0 # Sometimes 0 offset is implied

                abs_offset = base_offset + rel_offset

                # Resolve the underlying type
                if 'DW_AT_type' in child.attributes:
                    type_offset = child.attributes['DW_AT_type'].value
                    # pyelftools type offsets are relative to the CU, we need the absolute offset in the DWARF info
                    cu_offset = struct_die.cu.cu_offset
                    target_die = self.die_cache.get(cu_offset + type_offset)

                    if target_die:
                        # Follow typedefs to the base type
                        base_die = self._get_base_type(target_die)

                        if base_die and base_die.tag == 'DW_TAG_structure_type':
                            # It's an inline nested struct! Recurse.
                            nested = self._flatten_struct(base_die, f"{full_name}.", abs_offset)
                            fields.extend(nested)
                            continue

                        # Map to C Plugin FieldType
                        c_type, size = self._map_to_c_type(base_die, child)
                        fields.append({
                            "name": full_name,
                            "offset": abs_offset,
                            "size": size,
                            "type": c_type
                        })
        return fields

    def _get_base_type(self, die):
        """Follows DW_AT_type chains through typedefs/const/volatile to find the actual type."""
        current_die = die
        while current_die and current_die.tag in ('DW_TAG_typedef', 'DW_TAG_const_type', 'DW_TAG_volatile_type'):
            if 'DW_AT_type' not in current_die.attributes:
                return None
            type_offset = current_die.attributes['DW_AT_type'].value
            current_die = self.die_cache.get(current_die.cu.cu_offset + type_offset)
        return current_die

    def _map_to_c_type(self, base_die, member_die):
        """Heuristics to map DWARF types to the  C engine enum."""
        if not base_die:
            return FIELD_UINT32, 4 # Fallback

        if base_die.tag == 'DW_TAG_pointer_type':
            # Check if it's a char* (String pointer)
            if 'DW_AT_type' in base_die.attributes:
                ptr_target = self.die_cache.get(base_die.cu.cu_offset + base_die.attributes['DW_AT_type'].value)
                ptr_base = self._get_base_type(ptr_target)
                if ptr_base and ptr_base.tag == 'DW_TAG_base_type':
                    name = ptr_base.attributes.get('DW_AT_name', lambda: None)
                    if name and b'char' in name.value:
                        return FIELD_STRING_PTR, 4
            return FIELD_POINTER, 4

        elif base_die.tag == 'DW_TAG_array_type':
            # Check if it's an inline char array (e.g., pcTaskName[16])
            size = self._calculate_array_size(base_die)
            return FIELD_STRING_INLINE, size

        elif base_die.tag == 'DW_TAG_base_type':
            size = base_die.attributes.get('DW_AT_byte_size', None)
            size_val = size.value if size else 4
            if size_val == 1:
                return FIELD_UINT8, 1
            elif size_val == 2:
                return FIELD_UINT16, 2
            else:
                return FIELD_UINT32, 4

        return FIELD_UINT32, 4 # Default fallback

    def dump_all_struct_names(self):
        """Prints every struct name found in the DWARF info to help find the right targets."""
        found_structs = set()
        for cu in self.dwarf.iter_CUs():
            for die in cu.iter_DIEs():
                if die.tag == 'DW_TAG_structure_type' and 'DW_AT_name' in die.attributes:
                    name = die.attributes['DW_AT_name'].value.decode('utf-8')
                    found_structs.add(name)

        fastdyn_log.info(f"[*] Found {len(found_structs)} unique structs in DWARF:")
        for name in sorted(found_structs):
            fastdyn_log.info(f"  - {name}")

    def _calculate_array_size(self, array_die):
        """Calculates total byte size of an array from its subrange."""
        for child in array_die.iter_children():
            if child.tag == 'DW_TAG_subrange_type' and 'DW_AT_upper_bound' in child.attributes:
                # Array size is usually upper_bound + 1
                return child.attributes['DW_AT_upper_bound'].value + 1
        return 16 # Fallback size
