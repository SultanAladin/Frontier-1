# Viewer Card Prototypes

Each card is built and approved **standalone** before it enters the Component Viewer or Asset Viewer.
Rules: `Documentation/Rules/ViewerRedesignBrief.md`. Evidence: `Exhibits/ViewerRedesign/`.

| #  | Prototype                                   | Kind                    | Data                                    | Status                 |
|----|---------------------------------------------|-------------------------|-----------------------------------------|------------------------|
| 01 | [Tier Descent](TierDescent/TierDescent.html) | Interactive infographic | Real cluster tiers of ControlVehicle FCT1 parts | 🚧 awaiting approval |

## 01 · Tier Descent

Open `http://<host>:8095/Experimental/ComponentViewer/Prototypes/TierDescent/TierDescent.html`.

| Region            | Interaction                                                                 |
|-------------------|-----------------------------------------------------------------------------|
| Part chips        | Body · Tyre · Rim; rebuilds the cluster DAG in the page                     |
| Shed readout      | Large thin numeral with dimmed fraction; tweens on every change             |
| Source vs tier    | Hatched source block against a solid tier block with a reduction pill      |
| Material seams    | Free · Slide · Pin rebuilds the DAG; ring shows wrong-material share        |
| Silhouette        | Drag to turn; hover pins a cluster callout; Clusters · Materials · Clay     |
| Tier staircase    | Drag the dashed budget line; columns inside the budget brighten; the coarsest eligible tier is elected; click a column or rail row to force a tier; Auto returns to the budget |

📝 Tier deviation is the group error from `ClusterHierarchy.js` in model units (metres for the facets).
The 1 % / 6 % drift thresholds for Faithful / Drifting / Broken are prototype choices, not engine values.
