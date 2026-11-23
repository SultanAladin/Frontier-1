#!/usr/bin/env python3
"""Stage a narrow, reproducible compatibility patch; never edit ExternalPackages in place."""
from pathlib import Path
TAIL='''void* operator new(std::size_t size) {
    return tvg::malloc(size);
}


void operator delete(void* ptr) noexcept {
    tvg::free(ptr);
}'''
def stage(source:Path,destination:Path):
    original=(source/'src/renderer/tvgInitializer.cpp').read_text()
    if not original.endswith(TAIL):
        raise RuntimeError('ThorVG initializer differs from the reviewed 1.0.0 pin; re-review the allocation patch.')
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text(original[:-len(TAIL)]+'// Frontier: retain the standard C++ allocation functions.\n')
    return destination
if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('destination',type=Path);a=p.parse_args();stage(a.source,a.destination)
