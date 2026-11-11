#============================================================================================================================================
#                                                          STAGETHORVG.PY
#============================================================================================================================================
# 🧩 Stage ThorVG tvgInitializer.cpp into build scratch without global operator new/delete overrides; never mutate ExternalPackages/thorvg.

from pathlib import Path
import argparse

OperatorOverrideTail = '''void* operator new(std::size_t size) {
    return tvg::malloc(size);
}


void operator delete(void* ptr) noexcept {
    tvg::free(ptr);
}'''


def StageInitializer(SourceRoot: Path, DestinationFile: Path) -> Path:
    OriginalText = (SourceRoot / 'src/renderer/tvgInitializer.cpp').read_text(encoding='utf-8')
    if not OriginalText.endswith(OperatorOverrideTail):
        raise RuntimeError('ThorVG tvgInitializer.cpp differs from reviewed 1.0.0 pin; re-audit allocation staging.')
    DestinationFile.parent.mkdir(parents=True, exist_ok=True)
    DestinationFile.write_text(
        OriginalText[:-len(OperatorOverrideTail)] + '// Frontier: retain standard C++ global allocation operators.\n',
        encoding='utf-8',
    )
    return DestinationFile


if __name__ == '__main__':
    Parser = argparse.ArgumentParser()
    Parser.add_argument('source', type=Path)
    Parser.add_argument('destination', type=Path)
    Arguments = Parser.parse_args()
    StageInitializer(Arguments.source, Arguments.destination)
