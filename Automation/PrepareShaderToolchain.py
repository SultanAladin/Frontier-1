#============================================================================================================================================
#                                                    PREPARESHADERTOOLCHAIN.PY
#============================================================================================================================================
# 🧩 Download and SHA-256 verify the Slang (.slang) compiler and SwiftShader headless Vulkan ICD into ExternalPackages/VulkanCompute/.

from pathlib import Path
import hashlib
import io
import json
import subprocess
import sys
import tarfile
import urllib.request
import zipfile

RepositoryRoot = Path(__file__).resolve().parents[1]
ToolchainRoot  = RepositoryRoot / 'ExternalPackages/VulkanCompute'

WindowsHost        = sys.platform == 'win32'
SlangCompilerName  = 'slangc.exe' if WindowsHost else 'slangc'

# 📝 SPIR-V output is identical across hosts, so each host pins its own slangtorch 1.3.22 wheel carrying the same Slang release.
if WindowsHost:
    PinnedArtifacts = [
        {
            'Category': 'Slang',
            'Url': 'https://files.pythonhosted.org/packages/8e/e1/9d0da9dfd5b221db931ef88f5b7d8436f9ceffbe91896f0eafd11460fd06/slangtorch-1.3.22-py3-none-win_amd64.whl',
            'Sha256': '377891b2ed697b3a2bc4af53c7f2ae5b580a80251f6f672b461481137f47dff5',
            'Witness': 'Slang/slangtorch/bin/slangc.exe',
        },
    ]
else:
    PinnedArtifacts = [
        {
            'Category': 'Slang',
            'Url': 'https://files.pythonhosted.org/packages/59/db/f58a546564b8992e8e4c66b3e1b99432a87acec4317b10b1b9c7927868b9/slangtorch-1.3.22-py3-none-manylinux_2_27_x86_64.whl',
            'Sha256': '7755f2f53509ebbd65839dcd8a104e7105aeaf3e3600aea9385c2bec9beb2eaa',
            'Witness': 'Slang/slangtorch/bin/slangc',
        },
        {
            # Headless SwiftShader Vulkan ICD ships Linux shared objects only; Windows hosts use the installed GPU driver.
            'Category': 'SwiftShader',
            'Url': 'https://registry.npmjs.org/@sparticuz/chromium/-/chromium-153.0.0.tgz',
            'Sha256': '20e5656fcdcf4118ede85edfb9d6bb9cc1bbb7ffb8b75622fffbcf36a835c3c0',
            'Witness': 'SwiftShader/vk_swiftshader_icd.json',
        },
    ]


def Main():
    VerifyOnly = '--verify' in sys.argv[1:]
    ToolchainRoot.mkdir(parents=True, exist_ok=True)

    for Entry in PinnedArtifacts:
        Category = Entry['Category']
        WitnessPath = ToolchainRoot / Entry['Witness']
        StampPath = ToolchainRoot / (Category + '.sha256')

        if WitnessPath.exists() and StampPath.exists() and StampPath.read_text(encoding='utf-8').strip() == Entry['Sha256']:
            print('[Pinned]   {0} ({1})'.format(Category, Entry['Sha256'][:10]))
            continue

        if VerifyOnly:
            raise RuntimeError('Shader toolchain missing or unverified: ' + Category)

        print('[Fetch]    Downloading {0} ...'.format(Category), flush=True)
        with urllib.request.urlopen(Entry['Url'], timeout=120) as Response:
            Payload = Response.read()

        Digest = hashlib.sha256(Payload).hexdigest()
        if Digest != Entry['Sha256']:
            raise RuntimeError('SHA-256 mismatch for ' + Category)

        if Category == 'Slang':
            with zipfile.ZipFile(io.BytesIO(Payload)) as ZipBundle:
                for EntryName in ZipBundle.namelist():
                    if EntryName.startswith('slangtorch/bin/') or 'LICENSE' in EntryName.upper():
                        Destination = (ToolchainRoot / 'Slang' / EntryName).resolve()
                        if not Destination.is_relative_to((ToolchainRoot / 'Slang').resolve()):
                            raise RuntimeError('Unsafe Slang bundle entry: ' + EntryName)
                        if not EntryName.endswith('/'):
                            Destination.parent.mkdir(parents=True, exist_ok=True)
                            Destination.write_bytes(ZipBundle.read(EntryName))
            (ToolchainRoot / 'Slang/slangtorch/bin' / SlangCompilerName).chmod(0o755)
        elif Category == 'SwiftShader':
            with tarfile.open(fileobj=io.BytesIO(Payload), mode='r:gz') as OuterBundle:
                BrotliBytes = OuterBundle.extractfile('package/bin/swiftshader.tar.br').read()
            BrotliFile = ToolchainRoot / 'swiftshader.tar.br'
            TarFile = ToolchainRoot / 'swiftshader.tar'
            BrotliFile.write_bytes(BrotliBytes)
            try:
                subprocess.run(
                    [
                        'node', '-e',
                        'const f=require("fs"),z=require("zlib");'
                        'f.writeFileSync(process.argv[2],z.brotliDecompressSync(f.readFileSync(process.argv[1])));',
                        str(BrotliFile), str(TarFile),
                    ],
                    check=True,
                )
                with tarfile.open(TarFile) as InnerBundle:
                    for BinaryName in ('libvulkan.so.1', 'libvk_swiftshader.so', 'vk_swiftshader_icd.json'):
                        Destination = ToolchainRoot / 'SwiftShader' / BinaryName
                        Destination.parent.mkdir(parents=True, exist_ok=True)
                        Destination.write_bytes(InnerBundle.extractfile(BinaryName).read())
            finally:
                BrotliFile.unlink(missing_ok=True)
                TarFile.unlink(missing_ok=True)

        StampPath.write_text(Entry['Sha256'] + '\n', encoding='utf-8')
        print('[Verified] {0} -> {1}'.format(Category, WitnessPath.relative_to(RepositoryRoot)), flush=True)

    (ToolchainRoot / 'Manifest.json').write_text(json.dumps(PinnedArtifacts, indent=2) + '\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(Main())
    except Exception as Error:
        print('Shader toolchain setup failed:', Error, file=sys.stderr)
        sys.exit(1)
