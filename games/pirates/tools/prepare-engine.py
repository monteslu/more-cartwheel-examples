"""Check or apply Pirates' engine fixes without resetting either checkout."""
import argparse
from pathlib import Path
import subprocess
import sys


def git(repo, *args):
    return subprocess.run(["git", "-C", str(repo), *args], text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine", type=Path)
    parser.add_argument("--apply", action="store_true",
                        help="apply missing patches; otherwise check only")
    args = parser.parse_args()
    patches = Path(__file__).resolve().parents[1] / "engine-patches"
    requirements = (
        (args.engine, "6f16bbf19b5a3b439c40d19e60104110e8f0a49c", "water-pause.patch"),
        (args.engine, "6f16bbf19b5a3b439c40d19e60104110e8f0a49c", "scene-pause.patch"),
        (args.engine, "6f16bbf19b5a3b439c40d19e60104110e8f0a49c", "water-shore-refraction.patch"),
        (args.engine / "deps/three.c", "5c882fc1accdc2243608fd4d373911622d4a9164", "external-encoder.patch"),
    )
    pending = []
    # Preflight every checkout before changing anything. Never fetch/reset a
    # developer's engine or silently accept a different dependency revision.
    for repo, revision, name in requirements:
        head = git(repo, "rev-parse", "HEAD")
        if head.returncode or head.stdout.strip() != revision:
            raise RuntimeError(f"{repo}: expected HEAD {revision}; prepare the documented checkout first")
        patch = str(patches / name)
        if git(repo, "apply", "--reverse", "--check", patch).returncode == 0:
            print(f"{name}: already applied")
        elif git(repo, "apply", "--check", patch).returncode == 0:
            print(f"{name}: missing, applies cleanly")
            pending.append((repo, patch))
        else:
            raise RuntimeError(f"{repo}: {name} conflicts with local changes; nothing applied")
    if pending and not args.apply:
        print("Run again with --apply to install the missing fixes.", file=sys.stderr)
        return 1
    for repo, patch in pending:
        result = git(repo, "apply", patch)
        if result.returncode:
            raise RuntimeError(result.stderr)
        print(f"{Path(patch).name}: applied")
    print("Pirates engine fixes ready.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except RuntimeError as error:
        sys.exit(str(error))
