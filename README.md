# Frontier

Browser previews of the two editor prototypes imported from [SultanAladin/Frontier at `63db120`](https://github.com/SultanAladin/Frontier/tree/63db1201d0cea199c7276c2126e62812b04821b9).

## Run the editors

Start the servers in separate terminals from the repository root:

| Editor | Command | Port |
| --- | --- | --- |
| [Project Zero Editor](Experimental/ProjectZeroEditor/) | `python3 -m http.server --bind 0.0.0.0 --directory Experimental/ProjectZeroEditor 8092` | `8092` |
| [Frontier Editor](Experimental/FrontierEditor/) | `cd Experimental/FrontierEditor && npm ci && npm run dev -- --port 5173 --strictPort` | `5173` |

Project Zero serves the checked-in, self-contained `index.html`. After editing its source, run `node Experimental/ProjectZeroEditor/Build.mjs` from the repository root (after installing Frontier Editor’s npm dependencies) to regenerate the page.

Only the two editor directories are included. The Frontier Editor's native Construct bridge and Project Zero's separate Fracture workspace are not part of these previews.
