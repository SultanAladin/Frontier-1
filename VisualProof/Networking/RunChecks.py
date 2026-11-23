"""Exercise real linked SDK startup and fail-closed behavior; never attempts account login."""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--slate-root', required=True, type=pathlib.Path)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2]
    proof = pathlib.Path(__file__).resolve().parent
    project = root / 'Frontier/Projects/Project-Networking'
    output = project / 'Build/Output'
    include = args.slate_root.resolve() / 'Frontier/Engine/ProjectInterchange'
    environment = dict(os.environ)
    for key in ('EOS_CLIENT_SECRET', 'EOS_CLIENT_ID', 'EOS_ALLOW_CREATE_USER',
                'EOS_LOGIN_METHOD', 'EOS_DEVELOPER_CREDENTIAL'):
        environment.pop(key, None)
    lines = ['Project-Networking real SDK checks',
             'utc=' + datetime.datetime.now(datetime.timezone.utc).isoformat(),
             'authentication=NOT_ATTEMPTED; no client secret supplied']

    def run(command, expected=0):
        result = subprocess.run([str(x) for x in command], text=True, capture_output=True,
                                timeout=30, env=environment)
        lines.extend([result.stdout.rstrip(), result.stderr.rstrip(), 'exit=' + str(result.returncode)])
        if result.returncode != expected:
            raise RuntimeError('Unexpected exit from ' + str(command[0]))
        return result.stdout

    try:
        for _ in range(2):
            text = run([output / 'LoginHost', '--sdk-check'])
            if text.count('EOS_Success') != 2 or 'authentication=NOT_ATTEMPTED' not in text:
                raise RuntimeError('SDK initialization/shutdown did not both succeed')
        platform = run([output / 'LoginHost', '--platform-check'])
        if 'platform=created' not in platform or 'authentication=NOT_ATTEMPTED' not in platform:
            raise RuntimeError('Platform creation was not checked')
        lifecycle = run([output / 'LoginHost', '--lifecycle-check'])
        if (lifecycle.count('sdk_initialized_once=1') != 1 or lifecycle.count('sdk_shutdown_once=1') != 1
                or 'EOS_AlreadyConfigured' in lifecycle or 'PASS same-process SDK reuse' not in lifecycle):
            raise RuntimeError('SDK lifetime regression')
        text = run([output / 'LoginHost'], expected=2)
        if 'missing_EOS_CLIENT_SECRET' not in text or 'LOGIN_VERIFIED' in text:
            raise RuntimeError('Credential refusal did not fail closed')
        lines.append('PASS real executable refuses missing credentials without authentication')
        run([output / 'LoginHost', '--invalid'], expected=2)
        for source in ('LoginSequenceChecks', 'LobbyChecks', 'TransportChecks', 'InterchangeChecks'):
            executable = output / source
            run(['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I' + str(include),
                 proof / (source + '.cpp'), '-ldl', '-o', executable])
            command = [executable]
            if source == 'InterchangeChecks':
                command.append(output / 'ProjectNetworking.so')
            run(command)
        lines.append('PASS all checks; LIVE EOS AUTHENTICATION NOT RUN; WINDOWS MSVC NOT RUN')
        evidence = json.loads((output / 'LinuxBuildEvidence.json').read_text())
        evidence['check_sources'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                     for p in sorted(proof.iterdir()) if p.suffix in ('.cpp', '.py')}
        (proof / 'SdkBuildEvidence.json').write_text(json.dumps(evidence, indent=2) + '\n')
    except Exception as error:
        lines.append('FAIL ' + str(error))
        raise
    finally:
        text = '\n'.join(line for line in lines if line) + '\n'
        (proof / 'SdkChecks.log').write_text(text)
        print(text, end='')


if __name__ == '__main__':
    main()
