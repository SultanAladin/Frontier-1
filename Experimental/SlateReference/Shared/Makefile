# Slate — sandbox build. Module.toml carries the declarative unit map; Build/Construct.ps1 is the real build.
#
#   make sequence   run Construct.ps1's sequence (patches, partition, syntax-check every unit)  ← start here
#   make partition  prove the dependency partition alone
#   make patches    apply Slate's ImGui divergence
#   make codeindex  regenerate the .symbolindex digests
#   make check      gate the committed VisualProof PNGs against their applied inks
#   make clean      remove sandbox build output
#
# 🔴 `all` deliberately does NOT link a host. Every host in Engine/Application rides HostLifecycle, which
#    needs the Vulkan SDK, GLFW and a window server — none of which a sandbox has. The previous Makefile
#    named Engine/Application/OutlinerHost and PanelValidationHost, two headless hosts that no longer
#    exist; `make` therefore died on a missing source before reaching anything real. What a POSIX box CAN
#    prove is that every translation unit is accepted, in dependency order, with the ImGui patches applied
#    — that is `make sequence`, and it is the closest honest analogue of a Windows build.

PYTHON ?= python3

.PHONY: all sequence partition patches patches-verify codeindex proof check clean window-note

all: sequence

# 📝 The whole Construct.ps1 order in one step: unit graph, subject uniqueness, topological order, ImGui
#    patches, partition proof, then translation. Running the pieces out of this order is what made a
#    sandbox result disagree with a Windows one.
sequence:
	$(PYTHON) Scripts/ConstructSandbox.py

partition:
	$(PYTHON) Scripts/VerifyPartition.py

patches:
	$(PYTHON) Scripts/ApplyImGuiPatches.py

patches-verify:
	$(PYTHON) Scripts/ApplyImGuiPatches.py --verify

codeindex:
	$(PYTHON) Scripts/RunSymbolIndex.py build

# 🔴 The proof shots are committed under VisualProof/ and the gate reads those PNGs; it does not re-render
#    them. Re-rendering needs the headless hosts that this commit replaced with window hosts, so `proof`
#    reports that rather than pretending to produce shots.
proof:
	@echo "proof shots are rendered by the window hosts through Build/Construct.ps1;"
	@echo "the committed PNGs under VisualProof/ are gated by: make check"

check:
	$(PYTHON) Tools/AssertProofs.py

clean:
	rm -rf _AgentScratch/build _AgentScratch/logs

window-note:
	@echo "OutlinerWindowHost and PanelValidationWindowHost build through Build/Construct.ps1"
