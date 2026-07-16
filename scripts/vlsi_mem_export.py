#!/usr/bin/env python3

import argparse
import collections
import json
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path


CONF_REQUIRED = ("name", "depth", "width", "ports")
CONF_ALLOWED = ("name", "depth", "width", "ports", "mask_gran", "ruw")
CATALOG_HEADER = (
    "TYPE_NAME",
    "TYPE_ROLE",
    "FAMILY",
    "DEPTH_WORDS",
    "DATA_WIDTH_BITS",
    "PARAMETERS",
    "PORT_MODEL",
    "USED_BY",
    "SOURCE",
    "ACTION",
    "EVIDENCE",
)
CATALOG_LABELS = (
    "# LABELS\t"
    "UNITS: DEPTH_WORDS=addressable entries, DATA_WIDTH_BITS=bits per entry (b=bit, B=byte); "
    "TYPE_ROLE: EXT_LEAF=Firtool external memory, BASE_LEAF=storage Base, "
    "BASE_WRAPPER=Base that contains another storage Base, MACRO_LEAF=existing macro, "
    "BEHAVIORAL_LEAF=generated array; "
    "PORT_MODEL: 1R1W_SYNC/1R1W_SYNC_1CYCLE=synchronous 1-read+1-write, "
    "1R+1RW_ASYNC_READ=asynchronous read plus read/write view, "
    "1RW_ASYNC_READ=asynchronous read/write view, "
    "2R1W_BYTE_MASK_2CYCLE/1R1W_BYTE_MASK_2CYCLE=UltraRAM-style byte-masked ports, "
    "MACRO_DEFINED=defined by existing macro interface, "
    "ASYNC_READ_SYNC_WRITE[_MASKED]=generated asynchronous-read array; "
    "ACTION: REPLACE_REQUIRED=confirmed replacement, REVIEW_FOR_IP=manual IP review, "
    "ALREADY_MACRO_WRAPPED=existing macro wrapper"
)
BASE_TYPES = {
    "RF1R1WRamBase": (
        "BASE_LEAF",
        "SYNC_RF_BLACKBOX",
        ("addressWidth", "dataWidth"),
        "1R1W_SYNC_1CYCLE",
        "REPLACE_REQUIRED",
    ),
    "RF1R1RWAsyncRamBase": (
        "BASE_LEAF",
        "ASYNC_RF_BLACKBOX",
        ("addressWidth", "dataWidth", "selfReset"),
        "1R+1RW_ASYNC_READ",
        "REVIEW_FOR_IP",
    ),
    "RF1RWAsyncRamBase": (
        "BASE_LEAF",
        "ASYNC_RF_BLACKBOX",
        ("addressWidth", "dataWidth", "selfReset"),
        "1RW_ASYNC_READ",
        "REVIEW_FOR_IP",
    ),
    "DPIUltraRAM2R1WBase": (
        "BASE_WRAPPER",
        "ULTRARAM_RESOURCE",
        ("AWIDTH", "DWIDTH"),
        "2R1W_BYTE_MASK_2CYCLE",
        "REVIEW_FOR_IP",
    ),
    "UltraRAMBase": (
        "BASE_LEAF",
        "ULTRARAM_RESOURCE",
        ("AWIDTH", "DWIDTH"),
        "1R1W_BYTE_MASK_2CYCLE",
        "REVIEW_FOR_IP",
    ),
}
MODULE_RE = re.compile(
    r"^[ \t]*module[ \t]+([A-Za-z_][A-Za-z0-9_$]*)\b(.*?)^[ \t]*endmodule\b",
    re.MULTILINE | re.DOTALL,
)
RAMISH_RE = re.compile(r"ram|sram|uram|mem", re.IGNORECASE)


def fail(message):
    raise SystemExit(f"vlsi_mem_export: {message}")


def atomic_write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(text, encoding="utf-8")
    temporary.replace(path)


def parse_mem_conf(path):
    if not path.is_file():
        fail(f"missing memory configuration: {path}")

    records = []
    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line:
            continue
        tokens = line.split()
        if len(tokens) % 2:
            fail(f"{path}:{line_number}: expected key/value pairs")

        record = collections.OrderedDict()
        for index in range(0, len(tokens), 2):
            key, value = tokens[index : index + 2]
            if key not in CONF_ALLOWED:
                fail(f"{path}:{line_number}: unsupported field {key!r}")
            if key in record:
                fail(f"{path}:{line_number}: duplicate field {key!r}")
            record[key] = value

        missing = [key for key in CONF_REQUIRED if key not in record]
        if missing:
            fail(f"{path}:{line_number}: missing fields {', '.join(missing)}")
        for key in ("depth", "width", "mask_gran"):
            if key in record:
                try:
                    value = int(record[key])
                except ValueError:
                    fail(f"{path}:{line_number}: {key} must be an integer")
                if value <= 0:
                    fail(f"{path}:{line_number}: {key} must be positive")
        ports = record["ports"].split(",")
        if not ports or any(port not in ("read", "write", "readwrite") for port in ports):
            fail(f"{path}:{line_number}: unsupported ports {record['ports']!r}")
        records.append(record)

    if not records:
        fail(f"{path}: no memory records found")
    names = [record["name"] for record in records]
    if len(names) != len(set(names)):
        fail(f"{path}: duplicate memory module names")
    return records


def load_seq_mems(path):
    if not path.is_file():
        fail(f"missing Firtool memory metadata: {path}")
    try:
        records = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read {path}: {error}")
    if not isinstance(records, list):
        fail(f"{path}: expected a JSON array")
    return records


def validate_metadata(conf_records, metadata_records, metadata_path):
    metadata_by_name = {}
    for record in metadata_records:
        try:
            name = record["module_name"]
        except (KeyError, TypeError):
            fail(f"{metadata_path}: entry without module_name")
        if name in metadata_by_name:
            fail(f"{metadata_path}: duplicate module_name {name}")
        metadata_by_name[name] = record

    if set(metadata_by_name) != {record["name"] for record in conf_records}:
        fail("npu.mem.conf and seq_mems.json list different module names")

    for conf in conf_records:
        metadata = metadata_by_name[conf["name"]]
        for key in ("depth", "width"):
            if int(conf[key]) != metadata.get(key):
                fail(f"{conf['name']}: {key} differs between conf and metadata")
        conf_ports = collections.Counter(conf["ports"].split(","))
        metadata_ports = collections.Counter(
            {
                "read": metadata.get("read", 0),
                "write": metadata.get("write", 0),
                "readwrite": metadata.get("readwrite", 0),
            }
        )
        metadata_ports += collections.Counter()
        metadata_ports = collections.Counter(
            {key: value for key, value in metadata_ports.items() if value}
        )
        if conf_ports != metadata_ports:
            fail(f"{conf['name']}: ports differ between conf and metadata")
        if conf.get("ruw") != metadata.get("ruw_behavior"):
            fail(f"{conf['name']}: ruw differs between conf and metadata")


def normalized_conf(records):
    lines = []
    for record in records:
        fields = []
        for key in CONF_ALLOWED:
            if key in record and key != "ruw":
                fields.extend((key, record[key]))
        lines.append(" ".join(fields))
    return "\n".join(lines) + "\n"


def normalize(args):
    output_dir = Path(args.dir).resolve()
    conf_path = output_dir / "npu.mem.conf"
    metadata_path = output_dir / "metadata" / "seq_mems.json"
    records = parse_mem_conf(conf_path)
    metadata = load_seq_mems(metadata_path)
    validate_metadata(records, metadata, metadata_path)
    target = output_dir / "npu.mem.vlsi_mem_gen.conf"
    atomic_write(target, normalized_conf(records))
    print(f"validated {len(records)} external memories; wrote {target}")


def read_filelist(path):
    if not path.is_file():
        fail(f"missing filelist: {path}")
    return [line.strip() for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def write_filelists(output_dir):
    original = read_filelist(output_dir / "filelist.f")
    npu_mem = str((output_dir / "npu.mem.sv").resolve())
    rf_base = "RF1R1WRamBase.sv"
    if not any(Path(entry).name == rf_base for entry in original):
        fail(f"filelist.f does not contain {rf_base}")

    behavioral = [entry for entry in original if Path(entry).name != "npu.mem.sv"]
    behavioral.append(npu_mem)
    backend = [
        entry
        for entry in original
        if Path(entry).name not in ("npu.mem.sv", rf_base)
    ]
    atomic_write(output_dir / "filelist.behavioral.f", "\n".join(behavioral) + "\n")
    atomic_write(output_dir / "filelist.backend.f", "\n".join(backend) + "\n")


def load_modules(filelist):
    modules = {}
    texts = []
    for entry in filelist:
        path = Path(entry)
        if path.suffix.lower() not in (".sv", ".v") or not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        texts.append((path, text))
        for match in MODULE_RE.finditer(text):
            name = match.group(1)
            if name in modules:
                fail(f"duplicate module definition {name}: {modules[name][0]} and {path}")
            modules[name] = (path, match.group(0))
    return modules, texts


def module_instance_edges(modules):
    edges = {}
    parameterized = re.compile(
        r"^[ \t]*([A-Za-z_][A-Za-z0-9_$]*)[ \t]+#[ \t]*\(", re.MULTILINE
    )
    plain = re.compile(
        r"^[ \t]*([A-Za-z_][A-Za-z0-9_$]*)[ \t]+[A-Za-z_][A-Za-z0-9_$]*[ \t]*\(",
        re.MULTILINE,
    )
    module_names = set(modules)
    for owner, (_, text) in modules.items():
        children = collections.Counter()
        for pattern in (parameterized, plain):
            for module_name in pattern.findall(text):
                if module_name in module_names:
                    children[module_name] += 1
        edges[owner] = children
    return edges


def hierarchy_instance_counts(modules, top="Rvtsoc"):
    if top not in modules:
        fail(f"top module {top} is not defined in the behavioral filelist")
    edges = module_instance_edges(modules)
    counts = collections.Counter({top: 1})

    def walk(owner, multiplier, active):
        for child, local_count in edges[owner].items():
            if child in active:
                fail(f"recursive module hierarchy detected at {child}")
            child_count = multiplier * local_count
            counts[child] += child_count
            walk(child, child_count, active | {child})

    walk(top, 1, {top})
    return counts


def module_parents(edges):
    parents = collections.defaultdict(set)
    for owner, children in edges.items():
        for child in children:
            parents[child].add(owner)
    return parents


def relative_evidence_path(path, output_dir):
    try:
        return str(path.resolve().relative_to(output_dir.parent.parent.resolve()))
    except ValueError:
        return str(path.resolve())


def rf_base_rows(modules, hierarchy_counts, output_dir):
    rows = []
    pattern = re.compile(
        r"\bRF1R1WRamBase\s*#\s*\(\s*\.addressWidth\s*\(\s*(\d+)\s*\)\s*,"
        r"\s*\.dataWidth\s*\(\s*(\d+)\s*\)",
        re.DOTALL,
    )
    for owner, (path, body) in sorted(modules.items()):
        for match in pattern.finditer(body):
            address_width = int(match.group(1))
            width = int(match.group(2))
            rows.append(
                {
                    "CATEGORY": "RVT_RF_BASE",
                    "STATUS": "REPLACE_REQUIRED",
                    "MODULE": "RF1R1WRamBase",
                    "DEPTH": str(1 << address_width),
                    "WIDTH": str(width),
                    "PORTS": "read,write",
                    "RUW": "UNSPECIFIED",
                    "INSTANCE_COUNT": str(hierarchy_counts[owner]),
                    "DEFINE_FILE": relative_evidence_path(
                        modules["RF1R1WRamBase"][0], output_dir
                    ),
                    "EVIDENCE": f"{owner} parameters addressWidth={address_width},dataWidth={width}",
                }
            )
    if not rows:
        fail("no RF1R1WRamBase parameter groups found")
    return rows


def ext_rows(conf_records, metadata_records):
    metadata_by_name = {record["module_name"]: record for record in metadata_records}
    rows = []
    for conf in conf_records:
        metadata = metadata_by_name[conf["name"]]
        hierarchy = metadata.get("hierarchy", [])
        rows.append(
            {
                "CATEGORY": "FIRTOOL_SEQ_MEM",
                "STATUS": "REPLACE_REQUIRED",
                "MODULE": conf["name"],
                "DEPTH": conf["depth"],
                "WIDTH": conf["width"],
                "PORTS": conf["ports"],
                "RUW": conf.get("ruw", "UNSPECIFIED"),
                "INSTANCE_COUNT": str(len(hierarchy)),
                "DEFINE_FILE": "generated/npu-vlsi/npu.mem.sv",
                "EVIDENCE": "seq_mems.json:" + ",".join(hierarchy),
            }
        )
    return rows


def audit_rows(output_dir):
    conf_records = parse_mem_conf(output_dir / "npu.mem.conf")
    metadata_path = output_dir / "metadata" / "seq_mems.json"
    metadata_records = load_seq_mems(metadata_path)
    validate_metadata(conf_records, metadata_records, metadata_path)
    filelist = read_filelist(output_dir / "filelist.behavioral.f")
    modules, texts = load_modules(filelist)
    if "RF1R1WRamBase" not in modules:
        fail("behavioral filelist does not define RF1R1WRamBase")
    hierarchy_counts = hierarchy_instance_counts(modules)

    rows = ext_rows(conf_records, metadata_records)
    rows.extend(rf_base_rows(modules, hierarchy_counts, output_dir))
    required_modules = {row["MODULE"] for row in rows}

    unpacked_array = re.compile(
        r"\b(?:reg|logic)\b[^;\n]*\[[^\]]+\][^;\n]*\b[A-Za-z_][A-Za-z0-9_$]*\s*\[[^\]]+\]\s*;"
    )
    macro_marker = re.compile(r"\b(?:sram_array_[A-Za-z0-9_$]+|GENERIC_RAM_[A-Za-z0-9_$]+)\b")
    instance_marker = re.compile(
        r"^[ \t]*[A-Za-z_][A-Za-z0-9_$]*[ \t]+[A-Za-z_][A-Za-z0-9_$]*[ \t]*\(",
        re.MULTILINE,
    )
    for name, (path, body) in sorted(modules.items()):
        if not RAMISH_RE.search(name) or name in required_modules:
            continue
        if macro_marker.search(body) or name.startswith(("sram_array_", "GENERIC_RAM_")):
            status = "ALREADY_MACRO_WRAPPED"
            evidence = "module definition or child instance identifies an existing macro wrapper"
        elif unpacked_array.search(body):
            status = "BEHAVIORAL_REVIEW"
            evidence = "module contains an unpacked reg/logic array; replacement not inferred"
        elif instance_marker.search(body):
            status = "NOT_STORAGE_LEAF"
            evidence = "module is a structural container without a detected storage array"
        else:
            status = "UNKNOWN_REVIEW"
            evidence = "RAM-like name without sufficient storage-leaf evidence"
        rows.append(
            {
                "CATEGORY": "FULL_RAM_AUDIT",
                "STATUS": status,
                "MODULE": name,
                "DEPTH": "-",
                "WIDTH": "-",
                "PORTS": "-",
                "RUW": "-",
                "INSTANCE_COUNT": str(hierarchy_counts[name]),
                "DEFINE_FILE": relative_evidence_path(path, output_dir),
                "EVIDENCE": evidence,
            }
        )

    return sorted(
        rows,
        key=lambda row: (
            row["STATUS"] != "REPLACE_REQUIRED",
            row["CATEGORY"],
            row["MODULE"],
            int(row["DEPTH"]) if row["DEPTH"].isdigit() else -1,
            int(row["WIDTH"]) if row["WIDTH"].isdigit() else -1,
        ),
    )


def parameter_variant_users(type_name, modules, parameter_names):
    pattern = re.compile(
        rf"\b{re.escape(type_name)}\s*#\s*\((.*?)\)\s*[A-Za-z_][A-Za-z0-9_$]*\s*\(",
        re.DOTALL,
    )
    assignment = re.compile(r"\.([A-Za-z_][A-Za-z0-9_$]*)\s*\(\s*([^()]+?)\s*\)")
    variants = collections.defaultdict(set)
    for owner, (_, body) in modules.items():
        for match in pattern.finditer(body):
            values = dict(assignment.findall(match.group(1)))
            if all(name in values for name in parameter_names) and all(
                re.fullmatch(r"\d+", values[name].strip()) for name in parameter_names
            ):
                variant = ",".join(f"{name}={values[name]}" for name in parameter_names)
                variants[variant].add(owner)
    return variants


def behavioral_array_variants(body):
    declaration = re.compile(
        r"\b(?:reg|logic)\s+"
        r"(?:\[\s*(\d+)\s*:\s*(\d+)\s*\]\s+)?"
        r"([A-Za-z_][A-Za-z0-9_$]*)\s*"
        r"\[\s*(\d+)\s*:\s*(\d+)\s*\]\s*;"
    )
    variants = set()
    for packed_msb, packed_lsb, name, unpacked_left, unpacked_right in declaration.findall(body):
        write = re.search(
            rf"\b{re.escape(name)}\s*\[[^\]\n]+\]"
            rf"(?:\s*\[[^\]\n]+\])?\s*<=(?!=)",
            body,
        )
        references = len(re.findall(rf"\b{re.escape(name)}\s*\[", body))
        if not write or references < 3:
            continue
        width = abs(int(packed_msb) - int(packed_lsb)) + 1 if packed_msb else 1
        depth = abs(int(unpacked_left) - int(unpacked_right)) + 1
        variants.add((str(depth), str(width)))
    return variants


def generic_ram_spec(name):
    match = re.search(r"_1P(\d+)D(\d+)W(\d+)M$", name)
    if not match:
        return "-", "-", "module-fixed"
    depth, width, mask_parts = match.groups()
    return depth, width, f"mask_parts={mask_parts}"


def variant_dimensions(parameters):
    values = dict(field.split("=", 1) for field in parameters.split(",") if "=" in field)
    if "addressWidth" in values and "dataWidth" in values:
        return str(1 << int(values["addressWidth"])), values["dataWidth"]
    if "AWIDTH" in values and "DWIDTH" in values:
        return str(1 << int(values["AWIDTH"])), values["DWIDTH"]
    return "-", "-"


def catalog_row(
    name,
    role,
    family,
    depth_words,
    data_width_bits,
    parameters,
    port_model,
    used_by,
    source,
    action,
    evidence,
):
    return {
        "TYPE_NAME": name,
        "TYPE_ROLE": role,
        "FAMILY": family,
        "DEPTH_WORDS": depth_words,
        "DATA_WIDTH_BITS": data_width_bits,
        "PARAMETERS": parameters or "UNRESOLVED",
        "PORT_MODEL": port_model,
        "USED_BY": ",".join(sorted(used_by)) if used_by else "-",
        "SOURCE": source,
        "ACTION": action,
        "EVIDENCE": evidence,
    }


def memory_type_catalog(output_dir):
    conf_records = parse_mem_conf(output_dir / "npu.mem.conf")
    metadata_path = output_dir / "metadata" / "seq_mems.json"
    metadata_records = load_seq_mems(metadata_path)
    validate_metadata(conf_records, metadata_records, metadata_path)
    filelist = read_filelist(output_dir / "filelist.behavioral.f")
    modules, _ = load_modules(filelist)
    edges = module_instance_edges(modules)
    parents = module_parents(edges)
    reachable = hierarchy_instance_counts(modules)
    rows = []

    metadata_by_name = {record["module_name"]: record for record in metadata_records}
    for conf in conf_records:
        metadata = metadata_by_name[conf["name"]]
        rows.append(
            catalog_row(
                conf["name"],
                "EXT_LEAF",
                "FIRTOOL_SEQ_MEM",
                conf["depth"],
                conf["width"],
                f"ports={conf['ports']},ruw={conf.get('ruw', 'UNSPECIFIED')}",
                "1R1W_SYNC",
                parents[conf["name"]],
                "generated/npu-vlsi/npu.mem.conf",
                "REPLACE_REQUIRED",
                "seq_mems.json:" + ",".join(metadata.get("hierarchy", [])),
            )
        )

    for name, (role, family, parameters, port_model, action) in BASE_TYPES.items():
        if name not in modules or not reachable[name]:
            continue
        variant_users = parameter_variant_users(name, modules, parameters)
        if name == "UltraRAMBase":
            inherited = parameter_variant_users(
                "DPIUltraRAM2R1WBase", modules, ("AWIDTH", "DWIDTH")
            )
            variant_users = {variant: {"DPIUltraRAM2R1WBase"} for variant in inherited}
        if not variant_users:
            variant_users = {"UNRESOLVED": parents[name]}
        for variant, users in sorted(variant_users.items()):
            depth_words, data_width_bits = variant_dimensions(variant)
            rows.append(
                catalog_row(
                    name,
                    role,
                    family,
                    depth_words,
                    data_width_bits,
                    variant,
                    port_model,
                    users,
                    relative_evidence_path(modules[name][0], output_dir),
                    action,
                    "explicit memory Base type reachable from Rvtsoc",
                )
            )

    for name, (path, _) in sorted(modules.items()):
        if not reachable[name] or not name.startswith("GENERIC_RAM_"):
            continue
        depth_words, data_width_bits, parameters = generic_ram_spec(name)
        rows.append(
            catalog_row(
                name,
                "MACRO_LEAF",
                "EXISTING_MACRO",
                depth_words,
                data_width_bits,
                parameters,
                "MACRO_DEFINED",
                parents[name],
                relative_evidence_path(path, output_dir),
                "ALREADY_MACRO_WRAPPED",
                "instantiated by an sram_array_* wrapper",
            )
        )

    catalog_names = {row["TYPE_NAME"] for row in rows}
    for name, (path, body) in sorted(modules.items()):
        if not reachable[name] or name in catalog_names:
            continue
        variants = behavioral_array_variants(body)
        if not variants:
            continue
        port_model = "ASYNC_READ_SYNC_WRITE_MASKED" if "W0_mask" in body else "ASYNC_READ_SYNC_WRITE"
        for depth_words, data_width_bits in sorted(variants):
            rows.append(
                catalog_row(
                    name,
                    "BEHAVIORAL_LEAF",
                    "LOWERED_ASYNC_MEM",
                    depth_words,
                    data_width_bits,
                    "-",
                    port_model,
                    parents[name],
                    relative_evidence_path(path, output_dir),
                    "REVIEW_FOR_IP",
                    "module defines a reachable unpacked array with indexed reads and writes",
                )
            )

    catalog_names = {row["TYPE_NAME"] for row in rows}
    base_candidates = {
        name
        for name in modules
        if reachable[name] and name.endswith("Base") and RAMISH_RE.search(name)
    }
    missing_bases = sorted(base_candidates - catalog_names)
    if missing_bases:
        fail(f"unclassified reachable memory Base types: {', '.join(missing_bases)}")

    return sorted(
        rows,
        key=lambda row: (row["TYPE_ROLE"], row["FAMILY"], row["TYPE_NAME"], row["PARAMETERS"]),
    )


def render_catalog(rows):
    lines = ["\t".join(CATALOG_HEADER)]
    for row in rows:
        lines.append("\t".join(str(row[column]).replace("\t", " ") for column in CATALOG_HEADER))
    lines.extend(("", CATALOG_LABELS))
    return "\n".join(lines) + "\n"


def finalize(args):
    output_dir = Path(args.dir).resolve()
    if not (output_dir / "npu.mem.sv").is_file():
        fail(f"missing behavioral model: {output_dir / 'npu.mem.sv'}")
    write_filelists(output_dir)
    catalog = render_catalog(memory_type_catalog(output_dir))
    generated_catalog = output_dir / "memory_type_catalog.tsv"
    atomic_write(generated_catalog, catalog)
    print(f"wrote dual filelists and {generated_catalog}")


def report_required_counts(rows):
    result = collections.Counter()
    for row in rows:
        if row["STATUS"] != "REPLACE_REQUIRED":
            continue
        result[(row["MODULE"], int(row["DEPTH"]), int(row["WIDTH"]))] += int(
            row["INSTANCE_COUNT"]
        )
    return result


def parse_rf_specialization(submodule, default_address_width=5, default_data_width=2048):
    if not submodule.startswith("RF1R1WRamBase"):
        return None
    address_width = default_address_width
    data_width = default_data_width
    suffix = submodule[len("RF1R1WRamBase") :]
    address_match = re.search(r"(?:^|_)A([0-9A-Fa-f]+)(?:_|$)", suffix)
    data_match = re.search(r"(?:^|_)D([0-9A-Fa-f]+)(?:_|$)", suffix)
    if address_match:
        address_width = int(address_match.group(1), 16)
    if data_match:
        data_width = int(data_match.group(1), 16)
    return 1 << address_width, data_width


def verilator_counts(output_dir, verilator, top):
    with tempfile.TemporaryDirectory(prefix="npu-vlsi-check-") as temporary:
        temporary = Path(temporary)
        xml_path = temporary / "design.xml"
        command = [
            verilator,
            "-Wno-DEPRECATED",
            "--xml-only",
            "-Wno-fatal",
            "--bbox-unsup",
            "-DSYNTHESIS",
            "--top-module",
            top,
            "-f",
            str(output_dir / "filelist.behavioral.f"),
            "--Mdir",
            str(temporary / "obj"),
            "--xml-output",
            str(xml_path),
        ]
        subprocess.run(command, cwd=output_dir.parent.parent, check=True)
        counts = collections.Counter()
        submodules = set()
        for event, element in ET.iterparse(xml_path, events=("start", "end")):
            if event == "start" and element.tag == "cell":
                submodule = element.attrib.get("submodname", "")
                submodules.add(submodule)
                if submodule in ("ram_ext", "ram_0_ext"):
                    counts[(submodule, 0, 0)] += 1
                specialization = parse_rf_specialization(submodule)
                if specialization:
                    counts[("RF1R1WRamBase", *specialization)] += 1
            if event == "end" and element.tag == "cells":
                break
            if event == "end":
                element.clear()
        return counts, submodules


def check(args):
    output_dir = Path(args.dir).resolve()
    rows = audit_rows(output_dir)
    catalog_rows = memory_type_catalog(output_dir)
    expected_catalog = render_catalog(catalog_rows)
    actual_catalog = (output_dir / "memory_type_catalog.tsv").read_text(encoding="utf-8")
    if actual_catalog != expected_catalog:
        fail("memory_type_catalog.tsv is stale or non-deterministic; rerun finalize")

    behavioral = read_filelist(output_dir / "filelist.behavioral.f")
    backend = read_filelist(output_dir / "filelist.backend.f")
    removed = {Path(entry).name for entry in behavioral if entry not in backend}
    if removed != {"npu.mem.sv", "RF1R1WRamBase.sv"}:
        fail(f"backend filelist removes unexpected definitions: {sorted(removed)}")

    expected = report_required_counts(rows)
    elaborated, submodules = verilator_counts(output_dir, args.verilator, args.top)
    metadata = {record["module_name"]: record for record in load_seq_mems(output_dir / "metadata" / "seq_mems.json")}
    actual = collections.Counter()
    for key, count in elaborated.items():
        module, depth, width = key
        if module in metadata:
            actual[(module, metadata[module]["depth"], metadata[module]["width"])] += count
        else:
            actual[key] += count
    if actual != expected:
        fail(f"Verilator hierarchy counts differ: expected {expected}, got {actual}")
    unreachable_types = sorted(
        row["TYPE_NAME"]
        for row in catalog_rows
        if not any(
            submodule == row["TYPE_NAME"] or submodule.startswith(row["TYPE_NAME"] + "__")
            for submodule in submodules
        )
    )
    if unreachable_types:
        fail(f"catalog types missing from Verilator hierarchy: {', '.join(unreachable_types)}")
    print("behavioral filelist elaborated successfully")
    for key, count in sorted(actual.items()):
        print(f"verified {key[0]} depth={key[1]} width={key[2]} instances={count}")


def main():
    parser = argparse.ArgumentParser(description="Prepare and verify NPU SRAM export artifacts")
    subparsers = parser.add_subparsers(dest="command", required=True)

    normalize_parser = subparsers.add_parser("normalize")
    normalize_parser.add_argument("--dir", required=True)
    normalize_parser.set_defaults(function=normalize)

    finalize_parser = subparsers.add_parser("finalize")
    finalize_parser.add_argument("--dir", required=True)
    finalize_parser.set_defaults(function=finalize)

    check_parser = subparsers.add_parser("check")
    check_parser.add_argument("--dir", required=True)
    check_parser.add_argument("--top", default="Rvtsoc")
    check_parser.add_argument("--verilator", default="verilator")
    check_parser.set_defaults(function=check)

    args = parser.parse_args()
    args.function(args)


if __name__ == "__main__":
    main()
