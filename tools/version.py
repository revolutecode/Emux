#!/usr/bin/env python3
"""Ferramenta de versionamento da Emux.

version.json e a unica fonte de verdade. O CMake gera Version.hpp a partir dele
e a tag git vX.Y.Z tem de coincidir com o campo "compiler".

Uso:
    python tools/version.py show
    python tools/version.py check [--tag vX.Y.Z] [--require-clean]
    python tools/version.py bump {major|minor|patch|object}
    python tools/version.py set X.Y.Z
    python tools/version.py tag [--allow-branch]

Ver VERSIONING.md para as regras.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VERSION_FILE = ROOT / "version.json"
GENERATED_HEADER = ROOT / "include" / "Emux" / "Core" / "Version.hpp"
CMAKE_FILE = ROOT / "CMakeLists.txt"
CMAKE_TEMPLATE = ROOT / "cmake" / "Version.hpp.in"
CMAKE_SCRIPT = ROOT / "cmake" / "Version.cmake"
EXPECTED_KEYS = {"compiler", "object_format"}

SEMVER_RE = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(?:-((?:0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*)"
    r"(?:\.(?:0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*))*))?"
    r"(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$"
)


class VersionError(Exception):
    pass


def read_version() -> dict:
    try:
        data = json.loads(VERSION_FILE.read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise VersionError(f"{VERSION_FILE} nao existe")
    except json.JSONDecodeError as exc:
        raise VersionError(f"version.json invalido: {exc}")

    if not isinstance(data, dict):
        raise VersionError("version.json deve ser um objeto JSON")

    unknown = set(data) - EXPECTED_KEYS
    missing = EXPECTED_KEYS - set(data)
    if missing:
        raise VersionError(f"version.json: faltam campos {sorted(missing)}")
    if unknown:
        raise VersionError(f"version.json: campos desconhecidos {sorted(unknown)}")

    compiler = data["compiler"]
    if not isinstance(compiler, str) or not SEMVER_RE.match(compiler):
        raise VersionError(f"version.json: 'compiler' invalido: {compiler!r}")

    obj = data["object_format"]
    if not isinstance(obj, int) or isinstance(obj, bool) or obj < 1:
        raise VersionError(f"version.json: 'object_format' deve ser inteiro >= 1: {obj!r}")

    return {"compiler": compiler, "object_format": obj}


def write_version(compiler: str, object_format: int) -> None:
    if not SEMVER_RE.match(compiler):
        raise VersionError(f"versao SemVer invalida: {compiler}")
    if not isinstance(object_format, int) or object_format < 1:
        raise VersionError("object_format deve ser inteiro >= 1")

    text = json.dumps(
        {"compiler": compiler, "object_format": object_format},
        indent=4,
        ensure_ascii=False,
    )
    VERSION_FILE.write_text(text + "\n", encoding="utf-8")


def run_git(*args: str) -> str:
    result = subprocess.run(
        ["git", "-c", "core.multiPackIndex=false", *args],
        cwd=ROOT,
        text=True,
        capture_output=True,
        env={**os.environ, "GIT_TERMINAL_PROMPT": "0"},
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise VersionError(f"git {' '.join(args)} falhou: {detail}")
    return result.stdout.strip()


def is_clean() -> bool:
    return run_git("status", "--porcelain=v1", "--untracked-files=normal") == ""


def current_branch() -> str:
    return run_git("rev-parse", "--abbrev-ref", "HEAD")


def tag_exists(tag: str) -> bool:
    result = subprocess.run(
        ["git", "-c", "core.multiPackIndex=false", "rev-parse", "-q", "--verify", f"refs/tags/{tag}"],
        cwd=ROOT,
        text=True,
        capture_output=True,
        env={**os.environ, "GIT_TERMINAL_PROMPT": "0"},
    )
    return result.returncode == 0


def bump(version: str, part: str) -> str:
    match = SEMVER_RE.match(version)
    if not match:
        raise VersionError(f"versao SemVer invalida: {version}")

    major, minor, patch = (int(group) for group in match.group(1, 2, 3))

    if part == "major":
        return f"{major + 1}.0.0"
    if part == "minor":
        return f"{major}.{minor + 1}.0"
    if part == "patch":
        return f"{major}.{minor}.{patch + 1}"
    raise VersionError(f"parte desconhecida: {part}")


def check_structure() -> None:
    if GENERATED_HEADER.exists():
        raise VersionError(
            f"{GENERATED_HEADER} nao pode ser versionado: e gerado pelo CMake a partir de version.json"
        )

    for path in (CMAKE_FILE, CMAKE_TEMPLATE, CMAKE_SCRIPT):
        if not path.exists():
            raise VersionError(f"ficheiro obrigatorio em falta: {path.relative_to(ROOT)}")

    cmake_text = CMAKE_FILE.read_text(encoding="utf-8")
    for snippet in ("include(cmake/Version.cmake)", "configure_file(", "Version.hpp.in"):
        if snippet not in cmake_text:
            raise VersionError(
                f"CMakeLists.txt deixou de gerar Version.hpp a partir de version.json (falta '{snippet}')"
            )


def check(tag: str | None, require_clean: bool) -> dict:
    data = read_version()
    check_structure()

    expected_tag = f"v{data['compiler']}"

    if tag is not None:
        normalized = tag if tag.startswith("v") else f"v{tag}"
        if not SEMVER_RE.match(normalized[1:]):
            raise VersionError(f"tag fora do formato vX.Y.Z: {tag}")
        if "+" in normalized:
            raise VersionError(f"tags nao usam build metadata (+...): {tag}")
        if normalized != expected_tag:
            raise VersionError(
                f"tag {normalized} nao coincide com version.json ({expected_tag})"
            )

    if require_clean and not is_clean():
        raise VersionError("a arvore de trabalho nao esta limpa; faca commit antes")

    return data


def cmd_show(_args: argparse.Namespace) -> int:
    data = read_version()
    print(f"compiler:      {data['compiler']}")
    print(f"object format: {data['object_format']}")
    print(f"tag:           v{data['compiler']}")
    return 0


def cmd_check(args: argparse.Namespace) -> int:
    data = check(args.tag, args.require_clean)
    suffix = f", tag {args.tag}" if args.tag else ""
    print(f"ok: Emux {data['compiler']} (object format {data['object_format']}{suffix})")
    return 0


def cmd_bump(args: argparse.Namespace) -> int:
    data = read_version()

    if args.part == "object":
        object_format = data["object_format"] + 1
        compiler = data["compiler"]
    else:
        object_format = data["object_format"]
        compiler = bump(data["compiler"], args.part)

    write_version(compiler, object_format)
    print(f"compiler:      {data['compiler']} -> {compiler}")
    print(f"object format: {data['object_format']} -> {object_format}")
    print("Atualize a documentacao e faca commit antes de criar a tag.")
    return 0


def cmd_set(args: argparse.Namespace) -> int:
    data = read_version()
    if not SEMVER_RE.match(args.version):
        raise VersionError(f"versao SemVer invalida: {args.version}")
    write_version(args.version, data["object_format"])
    print(f"compiler:      {data['compiler']} -> {args.version}")
    return 0


def cmd_tag(args: argparse.Namespace) -> int:
    data = check(tag=None, require_clean=True)
    tag = f"v{data['compiler']}"

    branch = current_branch()
    if branch != "main" and not args.allow_branch:
        raise VersionError(
            f"tags so sao criadas a partir de 'main' (ramo atual: {branch}); use --allow-branch para forcar"
        )

    if tag_exists(tag):
        raise VersionError(f"a tag {tag} ja existe; tags nunca sao reutilizadas")

    run_git("tag", "-a", tag, "-m", f"Emux {data['compiler']}")
    print(f"tag {tag} criada em {run_git('rev-parse', '--short', 'HEAD')}")
    print(f"Para publicar: git push origin {tag}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Versoes da Emux: version.json -> CMake -> Version.hpp -> tag git."
    )
    sub = parser.add_subparsers(dest="command", required=True)

    show = sub.add_parser("show", help="mostra as versoes atuais")
    show.set_defaults(func=cmd_show)

    check_parser = sub.add_parser("check", help="valida version.json, CMake e (opcionalmente) a tag")
    check_parser.add_argument("--tag", help="tag git que deve coincidir com version.json")
    check_parser.add_argument("--require-clean", action="store_true", help="exige arvore de trabalho limpa")
    check_parser.set_defaults(func=cmd_check)

    bump_parser = sub.add_parser("bump", help="incrementa uma parte da versao")
    bump_parser.add_argument("part", choices=["major", "minor", "patch", "object"])
    bump_parser.set_defaults(func=cmd_bump)

    set_parser = sub.add_parser("set", help="define a versao do compilador (ex.: 0.3.0-rc.1)")
    set_parser.add_argument("version")
    set_parser.set_defaults(func=cmd_set)

    tag_parser = sub.add_parser("tag", help="cria a tag anotada vX.Y.Z (nao faz push)")
    tag_parser.add_argument("--allow-branch", action="store_true", help="permite ramos que nao sejam main")
    tag_parser.set_defaults(func=cmd_tag)

    return parser


def main(argv: list[str]) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except VersionError as exc:
        print(f"erro: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
