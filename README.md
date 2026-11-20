# Frontier

Two experimental browser editors imported from [SultanAladin/Frontier at `63db1201`](https://github.com/SultanAladin/Frontier/tree/63db1201d0cea199c7276c2126e62812b04821b9/Experimental). The rest of the engine repository is not included.

## Run the editors

**Project Zero Editor** — from the repository root:

```sh
python3 Experimental/ProjectZeroEditor/Serve.py 8092
```

Open `http://localhost:8092/`. The checked-in HTML is ready to serve without a build.

**Frontier Editor** — in a separate terminal from the repository root:

```sh
cd Experimental/FrontierEditor
npm ci
npm run dev -- --port 5173 --strictPort
```

Open `http://localhost:5173/`. Run `npm run build` from the editor directory to build the browser app. The native Construct backend is not part of this import, so commands that send creation requests to it are unavailable.
