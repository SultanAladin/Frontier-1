#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#============================================================================================================================================
#                                                              SYMBOLINDEX.PY
#============================================================================================================================================
# 🧩 Builds, verifies and queries the three-level `.symbolindex` symbol index that lets a reader answer
#    "what does this do, where does it live, what does it take, what does it return, who calls it"
#    without ever opening the source file.
#
# 📝 Pure standard library. No install, no third-party parser, no compile step.
#    python Tools/SymbolIndex.py build
#    python Tools/SymbolIndex.py check
#    python Tools/SymbolIndex.py find Normalize
#    python Tools/SymbolIndex.py read LinearAlgebra::Normalize

import argparse
import hashlib
import os
import re
import sys
import unicodedata

from dataclasses import dataclass, field

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")


#--------------------------------------------------------------------------------------------------------------------
#                                                  FORMAT CONSTANTS
#--------------------------------------------------------------------------------------------------------------------

FormatVersion  = "1.0"                                       # [-] - bumped only on a breaking layout change
IndexSuffix    = ".symbolindex"                              # [-] - one index file per folder, named after the folder
HeaderWidth    = 142                                         # [-] - `//===` ruler width, matches SKILL-Formatting
SectionWidth   = 122                                         # [-] - `//---` ruler width, matches SKILL-Formatting
DetailKeyWidth = 4                                           # [-] - widest detail key, `note` and `post`

SourceSuffixes = (".h", ".hpp", ".hh", ".inl", ".ipp",
                  ".c", ".cc", ".cpp", ".cxx",
                  ".comp", ".vert", ".frag", ".geom", ".tesc", ".tese", ".mesh", ".task", ".rgen",
                  ".glsl", ".hlsl", ".slang")

SkippedFolders = {".git", ".vs", ".idea", "_AgentScratch", "ExternalPackages",
                  "Binaries", "Intermediate", "Generated", "__pycache__"}

# Closed record kinds. A letter outside this set is a hard parse error, so drift is loud rather than silent.
RecordCategories = {
    "F": "function",                                         # free function, method, operator, constructor, destructor
    "T": "type",                                             # class, struct, union
    "M": "member",                                           # data member, emitted nested under its owning type
    "E": "enumeration",                                      # enum / enum class
    "V": "constant",                                         # namespace-scope constant or variable
    "A": "alias",                                            # using / typedef
    "K": "macro",                                            # #define
    "N": "namespace",                                        # namespace, emitted as scope only
    "X": "interop",                                          # extern "C" / vendor surface
    "S": "source",                                           # a source file row in the SOURCES section
    "I": "reference",                                        # a pointer to a child index file
}

# Closed annotation keys. Anything else on a `///` line is rejected by Validate().
AnnotationKeys = ("in", "out", "err", "use", "cost", "pre", "post", "note", "tag")

# Closed tag vocabulary. Zero shorthand, in line with SKILL-Naming.
KnownTags = {
    "api", "internal", "hot", "cold", "pure", "simd",
    "allocating", "nonallocating", "throwing", "nonthrowing",
    "threadsafe", "unsafe", "inline", "virtual", "static",
    "constructor", "destructor", "deprecated", "incomplete",
    "owning", "guarantee", "shared", "parity", "constexpr",
}

# ⚠️ `✔` (U+2714) and `✔️` (U+2714 U+FE0F) render identically and compare unequal. Both are accepted on
#    input and always normalised to the U+FE0F form on output, so an index file never carries two spellings.
CostScale = {"✔️": "✔️", "✔": "✔️", "🚩": "🚩", "🔴": "🔴"}
CostLegend = "✔️ low · 🚩 medium · 🔴 high (cost rises left to right)"

Absent = "-"                                                 # [-] - field is known to be empty
Unknown = "?"                                                # [-] - field was never authored; this is the work list

ReservedWords = {
    "if", "else", "for", "while", "switch", "case", "do", "return", "catch", "try",
    "sizeof", "alignof", "decltype", "static_assert", "throw", "new", "delete",
    "explicit", "friend", "operator", "template", "typename", "constexpr", "consteval",
    "noexcept", "public", "private", "protected", "using", "typedef", "namespace",
    "class", "struct", "union", "enum", "extern", "inline", "static", "virtual",
    "const", "volatile", "mutable", "register", "thread_local", "co_return", "co_await",
}

Identifier         = r"[^\W\d]\w*"
IdentifierPattern  = re.compile(Identifier)
TemplatePrefix     = re.compile(r"^\s*template\s*<")
SectionRuler       = re.compile(r"^\s*//-{4,}\s*$")
SectionTitle       = re.compile(r"^\s*//\s+(?P<Title>[^\s].*?)\s*$")
ModuleDescription  = re.compile(r"^\s*//\s*🧩\s*(?P<Text>.+?)\s*$")
AnnotationLine     = re.compile(r"^\s*///\s?(?P<Body>.*?)\s*$")
UnitToken          = re.compile(r"^\[[^\]]*\]$")
SpanParameter      = re.compile(r"^\w*SPAN\s*\(\s*" + Identifier + r"\s*,\s*(?P<Name>" + Identifier + r")\s*,")


#--------------------------------------------------------------------------------------------------------------------
#                                                 TEXT MEASUREMENT
#--------------------------------------------------------------------------------------------------------------------

def DisplayWidth(Text):
    """Terminal columns occupied by Text. Emoji and CJK count 2, variation selectors and joiners count 0."""
    Total = 0
    for Character in Text:
        Code = ord(Character)
        if Code in (0xFE0F, 0xFE0E, 0x200D) or unicodedata.combining(Character):
            continue
        if unicodedata.east_asian_width(Character) in ("W", "F"):
            Total += 2
        elif 0x1F300 <= Code <= 0x1FAFF or 0x2600 <= Code <= 0x27BF or 0x2B00 <= Code <= 0x2BFF:
            Total += 2
        else:
            Total += 1
    return Total


def Pad(Text, Width):
    """Left-align Text into a Width-column field, measured in display columns rather than code points."""
    return Text + " " * max(0, Width - DisplayWidth(Text))


def Centred(Text, Width):
    Free = max(0, Width - DisplayWidth(Text))
    return " " * (Free // 2) + Text


def Banner(Title, Width, Character):
    Rule = "//" + Character * (Width - 2)
    return [Rule, "//" + Centred(Title.upper(), Width - 2), Rule]


def ShortHash(Payload):
    return hashlib.sha1(Payload).hexdigest()[:8]


def Sanitised(Text):
    """`|` is the field separator and may never appear inside a field. Collapse whitespace while here."""
    return re.sub(r"\s+", " ", Text.replace("|", "/")).strip()


#--------------------------------------------------------------------------------------------------------------------
#                                                  SOURCE CLEANING
#--------------------------------------------------------------------------------------------------------------------

def CleanedText(Text):
    """Blank out comments and string bodies while preserving every character position and newline.

    Structural scanning runs on this copy so that a brace inside a string literal or a comment cannot
    move a symbol span. Annotation harvesting runs on the original text.
    """
    Output = list(Text)
    Position = 0
    Length = len(Text)
    while Position < Length:
        Character = Text[Position]
        if Character == "/" and Position + 1 < Length and Text[Position + 1] == "/":
            End = Text.find("\n", Position)
            End = Length if End < 0 else End
            for Cursor in range(Position, End):
                Output[Cursor] = " "
            Position = End
        elif Character == "/" and Position + 1 < Length and Text[Position + 1] == "*":
            End = Text.find("*/", Position + 2)
            End = Length if End < 0 else End + 2
            for Cursor in range(Position, End):
                if Text[Cursor] != "\n":
                    Output[Cursor] = " "
            Position = End
        elif Character == '"' and RawStringAt(Text, Position):
            Position = BlankRawString(Text, Output, Position)
        elif Character in ('"', "'"):
            Position = BlankQuoted(Text, Output, Position, Character)
        else:
            Position += 1
    return "".join(Output)


def RawStringAt(Text, QuotePosition):
    """True when the quote at QuotePosition opens a C++ raw string literal such as R"tag( ... )tag"."""
    Cursor = QuotePosition - 1
    if Cursor < 0 or Text[Cursor] != "R":
        return False
    Cursor -= 1
    while Cursor >= 0 and Text[Cursor] in "uUL8":
        Cursor -= 1
    return Cursor < 0 or not (Text[Cursor].isalnum() or Text[Cursor] == "_")


def BlankRawString(Text, Output, QuotePosition):
    Open = Text.find("(", QuotePosition)
    if Open < 0:
        return QuotePosition + 1
    Tag = Text[QuotePosition + 1:Open]
    Closer = ")" + Tag + '"'
    End = Text.find(Closer, Open)
    End = len(Text) if End < 0 else End + len(Closer)
    for Cursor in range(QuotePosition, End):
        if Text[Cursor] != "\n":
            Output[Cursor] = " "
    return End


def BlankQuoted(Text, Output, QuotePosition, Quote):
    Cursor = QuotePosition + 1
    Length = len(Text)
    while Cursor < Length:
        if Text[Cursor] == "\\":
            Cursor += 2
            continue
        if Text[Cursor] == Quote or Text[Cursor] == "\n":
            break
        Cursor += 1
    End = min(Cursor + 1, Length)
    for Position in range(QuotePosition, End):
        if Text[Position] != "\n":
            Output[Position] = " "
    return End


#--------------------------------------------------------------------------------------------------------------------
#                                                  RECORD STORAGE
#--------------------------------------------------------------------------------------------------------------------

@dataclass
class Parameter:
    Name: str = Absent
    Declared: str = Absent
    Unit: str = "[-]"
    Description: str = Unknown


@dataclass
class Symbol:
    Role: str = "F"
    Name: str = ""
    Owner: str = ""                                          # enclosing type or namespace, empty at file scope
    File: str = ""                                           # path relative to the folder holding the index
    First: int = 0
    Last: int = 0
    Section: str = "SYMBOLS"
    Signature: str = ""
    Returns: str = Absent
    Purpose: str = Unknown
    Tags: list = field(default_factory=list)
    Cost: str = Absent
    Inputs: list = field(default_factory=list)
    Outputs: list = field(default_factory=list)
    Failure: str = ""
    Uses: list = field(default_factory=list)
    Precondition: str = ""
    Postcondition: str = ""
    Notes: list = field(default_factory=list)
    Members: list = field(default_factory=list)              # nested M rows, populated for T and E records
    CallSites: list = field(default_factory=list)

    @property
    def Qualified(self):
        return "{0}::{1}".format(self.Owner, self.Name) if self.Owner else self.Name

    @property
    def Annotated(self):
        return self.Purpose != Unknown


@dataclass
class SourceRecord:
    Name: str = ""
    Lines: int = 0
    Hash: str = ""
    Symbols: int = 0
    Purpose: str = Unknown


@dataclass
class FolderRecord:
    Name: str = ""
    Path: str = ""                                           # relative to the scan root, forward slashes
    Layer: str = ""
    Purpose: str = Unknown
    Sources: list = field(default_factory=list)
    Symbols: list = field(default_factory=list)


#--------------------------------------------------------------------------------------------------------------------
#                                              ANNOTATION HARVESTING
#--------------------------------------------------------------------------------------------------------------------

def AnnotationAbove(OriginalLines, StartLine):
    """Collect the contiguous `///` block sitting immediately above StartLine (0-based, exclusive)."""
    Collected = []
    Cursor = StartLine - 1
    while Cursor >= 0:
        Match = AnnotationLine.match(OriginalLines[Cursor])
        if not Match:
            break
        Collected.append(Match.group("Body"))
        Cursor -= 1
    Collected.reverse()
    return Collected


def ApplyAnnotation(Target, Block, Problems, Where):
    """Fold a `///` block onto a harvested symbol. Unkeyed lines are purpose prose."""
    PurposeParts = []
    for Body in Block:
        if not Body:
            continue
        Head = Body.split(None, 1)
        Key = Head[0]
        Rest = Head[1].strip() if len(Head) > 1 else ""
        if Key not in AnnotationKeys:
            # Not a key, therefore prose. A leading 🧩 is decoration and is stripped rather than required,
            # so a missing or differently-encoded emoji can never break the parse.
            PurposeParts.append(Body.lstrip("🧩 ").strip())
            continue
        if Key == "in":
            Target.Inputs.append(ReadParameter(Rest, True))
        elif Key == "out":
            Target.Outputs.append(ReadParameter(Rest, False))
        elif Key == "err":
            Target.Failure = Rest
        elif Key == "use":
            Target.Uses.extend([Item.strip() for Item in Rest.split(",") if Item.strip()])
        elif Key == "cost":
            Symbol_ = CostScale.get(Rest.strip())
            if Symbol_ is None:
                Problems.append("{0}: unknown cost mark {1!r}, expected one of ✔️ 🚩 🔴".format(Where, Rest))
            else:
                Target.Cost = Symbol_
        elif Key == "pre":
            Target.Precondition = Rest
        elif Key == "post":
            Target.Postcondition = Rest
        elif Key == "note":
            Target.Notes.append(Rest)
        elif Key == "tag":
            for Item in [Piece.strip() for Piece in Rest.split(",") if Piece.strip()]:
                if Item not in KnownTags:
                    Problems.append("{0}: unknown tag {1!r}".format(Where, Item))
                elif Item not in Target.Tags:
                    Target.Tags.append(Item)
    if PurposeParts:
        Target.Purpose = Sanitised(" ".join(PurposeParts))


def ReadParameter(Text, Named):
    """Parse `in  𝑣  Vector3  [-]  description` or `out  Vector3  [-]  description`.

    The unit is the sole bracketed token. Everything before it is name and optional declared type,
    everything after it is prose. The declared type is advisory only; the signature is authoritative.
    """
    Entry = Parameter()
    Tokens = Text.split()
    UnitAt = next((Position for Position, Token in enumerate(Tokens) if UnitToken.match(Token)), -1)
    if UnitAt < 0:
        Leading, Trailing = Tokens, []
    else:
        Entry.Unit = Tokens[UnitAt]
        Leading, Trailing = Tokens[:UnitAt], Tokens[UnitAt + 1:]
    if Named and Leading:
        Entry.Name = Leading[0]
        if len(Leading) > 1:
            Entry.Declared = " ".join(Leading[1:])
    elif Leading:
        Entry.Declared = " ".join(Leading)
    if Trailing:
        Entry.Description = Sanitised(" ".join(Trailing))
    return Entry


#--------------------------------------------------------------------------------------------------------------------
#                                               STRUCTURAL SCANNING
#--------------------------------------------------------------------------------------------------------------------

def SpanEnd(CleanLines, StartLine):
    """Line index of the `}` closing the first `{` at or after StartLine, or StartLine when there is none."""
    Depth = 0
    Opened = False
    Cursor = StartLine
    Limit = len(CleanLines)
    while Cursor < Limit:
        for Character in CleanLines[Cursor]:
            if Character == "{":
                Depth += 1
                Opened = True
            elif Character == "}":
                Depth -= 1
                if Opened and Depth <= 0:
                    return Cursor
        Cursor += 1
    return StartLine


def LogicalStatement(CleanLines, Start):
    """Join physical lines from Start until parentheses balance and a terminator appears.

    Returns (Text, LastLine). Allman braces put the `{` on its own line, so a declaration and its opening
    brace are folded into one statement before classification.
    """
    Text = CleanLines[Start].strip()
    if not Text:
        return "", Start
    if Text.startswith("#"):
        Cursor = Start
        while Text.endswith("\\") and Cursor + 1 < len(CleanLines):
            Cursor += 1
            Text = Text[:-1] + " " + CleanLines[Cursor].strip()
        return Text, Cursor

    Cursor = Start
    Limit = min(len(CleanLines), Start + 48)
    while True:
        Depth = Text.count("(") - Text.count(")") + Text.count("<") - Text.count(">")
        Balanced = Text.count("(") == Text.count(")")
        if Balanced and (Text.endswith("{") or Text.endswith(";") or Text.endswith("}") or Text.endswith(":")):
            return Text, Cursor
        Cursor += 1
        if Cursor >= Limit:
            return CleanLines[Start].strip(), Start
        Addition = CleanLines[Cursor].strip()
        Text = (Text + " " + Addition).strip() if Addition else Text
        if not Addition and Balanced and Text.endswith(")"):
            # A declaration followed by a blank line is a dangling fragment, not a definition.
            return CleanLines[Start].strip(), Start


def WithoutTemplatePrefix(Text):
    """Strip a leading `template< ... >`, tracking angle depth so nested templates survive."""
    Match = TemplatePrefix.match(Text)
    if not Match:
        return Text, False
    Depth = 0
    for Position in range(Match.end() - 1, len(Text)):
        if Text[Position] == "<":
            Depth += 1
        elif Text[Position] == ">":
            Depth -= 1
            if Depth == 0:
                return Text[Position + 1:].strip(), True
    return Text, True


def MatchingParenthesis(Text, Open):
    Depth = 0
    for Position in range(Open, len(Text)):
        if Text[Position] == "(":
            Depth += 1
        elif Text[Position] == ")":
            Depth -= 1
            if Depth == 0:
                return Position
    return -1


def TopLevelSplit(Text, Separator=","):
    """Split on Separator at bracket depth zero.

    ⚠️ `<` and `>` are ambiguous — template brackets, comparisons, or halves of a shift. `<<` and `>>` are
    consumed as operators, and a `>` at zero angle depth is left alone, so `Gathering = 1u << 0` no longer
    opens a depth that never closes and swallows every following separator.
    """
    Pieces = []
    Depth = 0
    Angle = 0
    Current = []
    Position = 0
    Length = len(Text)
    while Position < Length:
        Character = Text[Position]
        Pair = Text[Position:Position + 2]
        if Pair in ("<<", ">>") and Angle == 0:
            Current.append(Pair)
            Position += 2
            continue
        if Character in "([{":
            Depth += 1
        elif Character in ")]}":
            Depth -= 1
        elif Character == "<":
            Angle += 1
        elif Character == ">" and Angle > 0:
            Angle -= 1
        if Character == Separator and Depth == 0 and Angle == 0:
            Pieces.append("".join(Current))
            Current = []
        else:
            Current.append(Character)
        Position += 1
    Pieces.append("".join(Current))
    return [Piece.strip() for Piece in Pieces if Piece.strip()]


def DeclaredParameters(ParameterText):
    """Split a parameter list into (Name, DeclaredType) pairs; unnamed parameters report `-`."""
    Result = []
    for Chunk in TopLevelSplit(ParameterText):
        Chunk = TopLevelSplit(Chunk, "=")[0].strip() if "=" in Chunk else Chunk
        if Chunk in ("void", ""):
            continue
        Chunk = re.sub(r"\[[^\]]*\]\s*$", "", Chunk).strip()

        # 📝 A span macro carries its parameter name inside the argument list — `SLATE_INOUT_SPAN(Real64,
        #    Expansion, Capacity)` closes on `)`, so the trailing-identifier rule below recovers nothing and
        #    the parameter reads as unnamed. The name is the second argument; the first is its type.
        SpanForm = SpanParameter.match(Chunk)
        if SpanForm:
            Result.append((SpanForm.group("Name"), Sanitised(Chunk)))
            continue

        Tokens = IdentifierPattern.findall(Chunk)
        if Tokens and Tokens[-1] not in ReservedWords and re.search(Identifier + r"\s*$", Chunk):
            Name = Tokens[-1]
            Declared = Chunk[:Chunk.rfind(Name)].strip() or Absent
        else:
            Name, Declared = Absent, Chunk
        Result.append((Name, Sanitised(Declared)))
    return Result


def SymbolName(Head):
    """Trailing identifier of a declaration head, handling `Type::Method`, `~Type` and `operator X`."""
    Head = Head.strip()
    if "operator" in Head:
        Position = Head.rfind("operator")
        Candidate = Sanitised(Head[Position:])
        if Candidate != "operator":
            return Candidate, Head[:Position].strip()
    Match = re.search(r"(~?" + Identifier + r"(?:\s*::\s*~?" + Identifier + r")*)\s*$", Head)
    if not Match:
        return "", Head
    Whole = re.sub(r"\s+", "", Match.group(1))
    return Whole, Head[:Match.start(1)].strip()


def ClassifyStatement(Text):
    """Reduce a logical statement to (Role, Name, Extra) or None when it declares nothing indexable."""
    Text = Text.strip()
    if not Text:
        return None

    if Text.startswith("#"):
        Macro = re.match(r"#\s*define\s+(" + Identifier + r")", Text)
        return ("K", Macro.group(1), {}) if Macro else None

    Text, Templated = WithoutTemplatePrefix(Text)
    if not Text:
        return None

    Leading = Text.split(None, 1)[0].rstrip(":")
    if Leading in ("public", "private", "protected"):
        return None

    Namespace = re.match(r"^namespace\s+(" + Identifier + r"(?:\s*::\s*" + Identifier + r")*)\s*\{?", Text)
    if Namespace:
        return "N", re.sub(r"\s+", "", Namespace.group(1)), {}

    Aggregate = re.match(r"^(?:class|struct|union)\s+(?:\w+_API\s+)?(" + Identifier + r")\b", Text)
    if Aggregate:
        Bases = re.search(r":\s*([^{;]+)", Text)
        return "T", Aggregate.group(1), {"Bases": Sanitised(Bases.group(1)) if Bases else "",
                                         "Templated": Templated,
                                         "Forward": Text.rstrip().endswith(";")}

    Enumeration = re.match(r"^enum\s+(?:class\s+|struct\s+)?(" + Identifier + r")\b", Text)
    if Enumeration:
        # 📝 The constants are NOT read from Text: LogicalStatement stops at the opening brace, so the body
        #    is not in hand here. HarvestFile re-reads them from the resolved span via EnumerationConstants.
        Underlying = re.search(r":\s*([^{;]+)", Text)
        return "E", Enumeration.group(1), {"Underlying": Sanitised(Underlying.group(1)) if Underlying else "",
                                           "Forward": Text.rstrip().endswith(";")}

    Alias = re.match(r"^using\s+(" + Identifier + r")\s*=\s*(.+?);\s*$", Text)
    if Alias:
        return "A", Alias.group(1), {"Target": Sanitised(Alias.group(2))}

    if Text.startswith("typedef "):
        Name, _ = SymbolName(Text.rstrip(";").strip())
        return ("A", Name, {"Target": Sanitised(Text[8:].rstrip(";"))}) if Name else None

    Open = Text.find("(")
    if Open >= 0:
        Close = MatchingParenthesis(Text, Open)
        if Close > 0:
            Head = Text[:Open]
            Tail = Text[Close + 1:].strip()
            Name, Prefix = SymbolName(Head)
            Terminated = "{" in Tail or Tail.endswith(";") or Tail == ""
            Acceptable = Tail == "" or Tail[0] in "{;:-&" or re.match(
                r"^(const|noexcept|override|final|volatile|mutable|requires|throw|try)\b", Tail)
            if Name and Terminated and Acceptable and Name.split("::")[-1].lstrip("~") not in ReservedWords:
                Arrow = re.search(r"->\s*([^;{]+)", Tail)
                return "F", Name, {"Parameters": Text[Open + 1:Close],
                                   "Returns": Sanitised(Arrow.group(1)) if Arrow else Sanitised(Prefix),
                                   "Tail": Tail,
                                   "Templated": Templated,
                                   "Declaration": "{" not in Tail}
        return None

    if Text.endswith(";"):
        Body = Text[:-1]
        Body = TopLevelSplit(Body, "=")[0].strip()
        Body = re.sub(r"\{.*\}\s*$", "", Body).strip()
        # An array declarator binds to the type, not the name — `uint32_t Buckets[16]` is an array of 16.
        # Strip it to find the name, then hand it back to the declared type so the extent is not lost.
        Extent = re.search(r"((?:\[[^\]]*\])+)\s*$", Body)
        Body = Body[:Extent.start(1)].strip() if Extent else Body
        Name, Prefix = SymbolName(Body)
        if Name and Prefix and Name not in ReservedWords and "::" not in Name:
            Constant = bool(re.match(r"^(?:constexpr|const|static|inline|extern|thread_local)\b", Prefix))
            Declared = Sanitised(Prefix) + (Extent.group(1) if Extent else "")
            return "V", Name, {"Declared": Declared, "Constant": Constant}
    return None


def EnumerationConstants(CleanLines, First, Last):
    """Enumerator names between the braces of an enum spanning First..Last inclusive (0-based).

    Read from the resolved span rather than from the classified statement, because a logical statement
    ends at the opening brace and never contains the body.
    """
    Body = "\n".join(CleanLines[First:Last + 1])
    Open = Body.find("{")
    Close = Body.rfind("}")
    if Open < 0 or Close < Open:
        return []
    Constants = []
    for Piece in TopLevelSplit(Body[Open + 1:Close]):
        Name = TopLevelSplit(Piece, "=")[0].strip()
        Match = re.match(r"^(" + Identifier + r")\b", Name)
        if Match:
            Constants.append(Match.group(1))
    return Constants


def SectionTitles(OriginalLines):
    """Line index → enclosing `//--- TITLE ---` section banner, so the index mirrors the file's own skeleton."""
    Titles = [None] * len(OriginalLines)
    Current = "SYMBOLS"
    Cursor = 0
    while Cursor < len(OriginalLines):
        if (SectionRuler.match(OriginalLines[Cursor])
                and Cursor + 2 < len(OriginalLines)
                and SectionRuler.match(OriginalLines[Cursor + 2])):
            Title = SectionTitle.match(OriginalLines[Cursor + 1])
            if Title:
                Current = Sanitised(Title.group("Title")).upper()
                Titles[Cursor] = Titles[Cursor + 1] = Titles[Cursor + 2] = Current
                Cursor += 3
                continue
        Titles[Cursor] = Current
        Cursor += 1
    return Titles


def HarvestFile(FullPath, ShortName, Problems):
    """Extract every indexable symbol from one source file."""
    Raw = ReadTextFile(FullPath)
    OriginalLines = Raw.splitlines()
    CleanLines = CleanedText(Raw).splitlines()
    while len(CleanLines) < len(OriginalLines):
        CleanLines.append("")
    Sections = SectionTitles(OriginalLines)

    Purpose = Unknown
    for Line in OriginalLines[:24]:
        Match = ModuleDescription.match(Line)
        if Match:
            Purpose = Sanitised(Match.group("Text"))
            break

    Harvested = []
    OwnerStack = []                                          # (Name, LastLine, Role)
    Cursor = 0
    while Cursor < len(CleanLines):
        while OwnerStack and Cursor > OwnerStack[-1][1]:
            OwnerStack.pop()

        Text, Final = LogicalStatement(CleanLines, Cursor)
        Classified = ClassifyStatement(Text) if Text else None
        if not Classified:
            Cursor += 1
            continue

        Role, Name, Extra = Classified
        Owner = "::".join(Entry[0] for Entry in OwnerStack if Entry[2] == "T")
        Opens = Text.rstrip().endswith("{") or "{" in Text
        Closing = SpanEnd(CleanLines, Cursor) if Opens else Final

        if Role == "N":
            OwnerStack.append((Name, Closing, "N"))
            Cursor = Final + 1
            continue

        if Role in ("T", "E") and Extra.get("Forward"):
            Cursor = Final + 1
            continue

        Entry = Symbol(Role=Role, Name=Name, Owner=Owner, File=ShortName,
                       First=Cursor + 1, Last=Closing + 1,
                       Section=Sections[Cursor] or "SYMBOLS",
                       Signature=Sanitised(Text.rstrip("{").rstrip()))

        if Role == "F":
            Entry.Returns = Extra["Returns"] or Absent
            Bare = Name.split("::")[-1]
            if Bare.startswith("~"):
                Entry.Tags.append("destructor")
                Entry.Returns = Absent
            elif Owner and Bare == Owner.split("::")[-1]:
                Entry.Tags.append("constructor")
                Entry.Returns = Absent
            if Extra.get("Templated"):
                Entry.Returns = Entry.Returns or Absent
            for ParameterName, Declared in DeclaredParameters(Extra["Parameters"]):
                Entry.Inputs.append(Parameter(Name=ParameterName, Declared=Declared, Description=Unknown))
            if Extra.get("Declaration"):
                Entry.Last = Entry.First
        elif Role == "E":
            Entry.Returns = Extra.get("Underlying") or Absent
            Entry.Members = [Parameter(Name=Constant, Declared=Name, Description=Unknown)
                             for Constant in EnumerationConstants(CleanLines, Cursor, Closing)]
        elif Role == "A":
            Entry.Returns = Extra.get("Target", Absent)
        elif Role == "V":
            Entry.Returns = Extra.get("Declared", Absent)
            # A `V` declared inside a type is a data member: it becomes an `M` folded onto that type,
            # never a row of its own. Only namespace-scope constants stay as rows.
            if Owner:
                Entry.Role = "M"

        Block = AnnotationAbove(OriginalLines, Cursor)
        if Block:
            ApplyAnnotation(Entry, Block, Problems, "{0}:{1}".format(ShortName, Cursor + 1))
            MergeAuthoredInputs(Entry, Problems, "{0}:{1}".format(ShortName, Cursor + 1))

        if Entry.Role == "M":
            FoldMember(Harvested, Owner, Entry)
        else:
            Harvested.append(Entry)

        if Role == "T" and Opens:
            OwnerStack.append((Name, Closing, "T"))
            Cursor = Final + 1
        elif Opens and Role in ("F", "E"):
            Cursor = Closing + 1
        else:
            Cursor = Final + 1

    Record = SourceRecord(Name=ShortName, Lines=len(OriginalLines),
                          Hash=ShortHash(Raw.encode("utf-8")), Symbols=len(Harvested), Purpose=Purpose)
    return Record, Harvested, CleanedText(Raw)


def FoldMember(Harvested, Owner, Entry):
    """Attach a data member to its owning `T` record as a `has` detail line.

    A member whose type was never harvested — one declared inside a forward-declared or macro-generated
    aggregate — is kept as a row of its own rather than dropped, because absence must mean "does not exist".
    """
    Host = next((Item for Item in reversed(Harvested)
                 if Item.Role == "T" and Item.Qualified == Owner), None)
    if Host is None:
        Entry.Role = "V"
        Harvested.append(Entry)
        return
    Host.Members.append(Parameter(Name=Entry.Name, Declared=Entry.Returns,
                                  Description=Entry.Purpose))


def MergeAuthoredInputs(Entry, Problems, Where):
    """Fold authored `in` descriptions onto the parameters recovered from the signature."""
    if not Entry.Inputs:
        return
    Authored = [Item for Item in Entry.Inputs if Item.Description != Unknown or Item.Unit != "[-]"]
    Recovered = [Item for Item in Entry.Inputs if Item not in Authored]
    if not Authored:
        return
    Merged = []
    Consumed = set()
    for Declared in Recovered:
        Match = next((Item for Item in Authored
                      if Item.Name == Declared.Name and id(Item) not in Consumed), None)
        if Match is None:
            Merged.append(Declared)
            continue
        Consumed.add(id(Match))
        Declared.Unit = Match.Unit
        Declared.Description = Match.Description
        if Match.Declared not in (Absent, Declared.Declared) and Declared.Declared != Absent:
            Problems.append("{0}: `in {1}` declares {2!r} but the signature says {3!r}".format(
                Where, Declared.Name, Match.Declared, Declared.Declared))
        Merged.append(Declared)
    for Item in Authored:
        if id(Item) not in Consumed:
            Problems.append("{0}: `in {1}` has no matching parameter".format(Where, Item.Name))
            Merged.append(Item)
    Entry.Inputs = Merged


#--------------------------------------------------------------------------------------------------------------------
#                                                  FOLDER TRAVERSAL
#--------------------------------------------------------------------------------------------------------------------

def ReadTextFile(FullPath):
    with open(FullPath, "r", encoding="utf-8", errors="replace", newline="") as Stream:
        return Stream.read().replace("\r\n", "\n").replace("\r", "\n")


def WriteTextFile(FullPath, Text):
    os.makedirs(os.path.dirname(FullPath), exist_ok=True)
    with open(FullPath, "w", encoding="utf-8", newline="\n") as Stream:
        Stream.write(Text)


def SourceFolders(Root):
    """Every folder beneath Root that directly holds at least one source file, in stable sorted order."""
    Found = []
    for Current, Children, Files in os.walk(Root):
        Children[:] = sorted(Name for Name in Children if Name not in SkippedFolders)
        Sources = sorted(Name for Name in Files if Name.endswith(SourceSuffixes))
        if Sources:
            Found.append((Current, Sources))
    return sorted(Found, key=lambda Entry: Entry[0].replace("\\", "/"))


def RelativePath(Root, FullPath):
    return os.path.relpath(FullPath, os.path.dirname(Root)).replace("\\", "/")


def LayerOf(Root, FolderPath):
    """First path component under Root; that is the L0..L6 layer folder."""
    Relative = os.path.relpath(FolderPath, Root).replace("\\", "/")
    if Relative in (".", ""):
        return ""
    return Relative.split("/")[0]


def ShortCallSite(Root, FolderPath, FileName):
    return "{0}/{1}".format(os.path.basename(FolderPath), FileName)


#--------------------------------------------------------------------------------------------------------------------
#                                                    CALL SITES
#--------------------------------------------------------------------------------------------------------------------

def ResolveCallSites(Folders, TokenSets, Limit=6):
    """Fill CallSites from a repository-wide identifier sweep.

    Resolution stops at file granularity on purpose: `Folder/File.cpp`, never `File.cpp:412`. A line number
    would make every unrelated edit dirty this index, and the reader only needs to know which file to open.
    """
    Owners = {}
    for Folder in Folders:
        for Entry in Folder.Symbols:
            Owners.setdefault(Entry.Name, []).append((Folder, Entry))

    Referencing = {}
    for SiteName, Tokens in TokenSets.items():
        for Token in Tokens:
            if Token in Owners:
                Referencing.setdefault(Token, set()).add(SiteName)

    for Name, Holders in Owners.items():
        Sites = sorted(Referencing.get(Name, ()))
        for Folder, Entry in Holders:
            Own = ShortCallSite(None, Folder.Path, Entry.File)
            External = [Site for Site in Sites if Site != Own]
            Entry.CallSites = External[:Limit]
            if len(External) > Limit:
                Entry.CallSites.append("(+{0} more)".format(len(External) - Limit))


#--------------------------------------------------------------------------------------------------------------------
#                                                     EMISSION
#--------------------------------------------------------------------------------------------------------------------

def FileHeader(Title, Description):
    Lines = Banner(Title, HeaderWidth, "=")
    Lines.append("// 🧩 {0}".format(Description))
    Lines.append("")
    return Lines


def Directives(Pairs):
    Width = max(DisplayWidth(Key) for Key, _ in Pairs)
    return ["%{0}  {1}".format(Pad(Key, Width), Content) for Key, Content in Pairs]


def Row(Columns, Widths):
    Cells = [Pad(Text, Width) for Text, Width in zip(Columns[:-1], Widths[:-1])]
    Cells.append(Columns[-1])
    return " | ".join(Cells).rstrip()


def DetailRow(Key, Columns, Widths):
    """An indented `key` line beneath a record header.

    Detail lines are space-aligned, not pipe-separated: the four-space indent already marks them as
    subordinate, and a second grid of pipes reads as noise beside the header row it belongs to.
    """
    Cells = [Pad(Text, Width) for Text, Width in zip(Columns[:-1], Widths[:-1])]
    Cells.append(Columns[-1])
    return "    {0}  {1}".format(Pad(Key, DetailKeyWidth), "  ".join(Cells)).rstrip()


def DetailNote(Key, Text):
    return "    {0}  {1}".format(Pad(Key, DetailKeyWidth), Sanitised(Text)).rstrip()


def ColumnWidths(Rows):
    if not Rows:
        return []
    Count = len(Rows[0])
    return [max(DisplayWidth(Entry[Position]) for Entry in Rows) for Position in range(Count)]


def SpanText(Entry):
    return "{0}-{1}".format(Entry.First, Entry.Last) if Entry.Last > Entry.First else str(Entry.First)


def EmitFolderIndex(Folder):
    """Full records: one header row per symbol plus indented detail lines. This is the only level that
    carries parameters, failure behaviour and call sites."""
    Lines = FileHeader(Folder.Name + IndexSuffix, Folder.Purpose)

    Annotated = sum(1 for Entry in Folder.Symbols if Entry.Annotated)
    Lines += Directives([
        ("format", "symbolindex " + FormatVersion),
        ("scope", "folder"),
        ("path", Folder.Path),
        ("layer", Folder.Layer or Absent),
        ("sources", str(len(Folder.Sources))),
        ("symbols", str(len(Folder.Symbols))),
        ("annotated", "{0}/{1}".format(Annotated, len(Folder.Symbols))),
        ("cost", CostLegend),
    ])
    Lines.append("")

    Lines += Banner("Sources", SectionWidth, "-")
    Lines.append("")
    SourceRows = [["S " + Item.Name, "{0} lines".format(Item.Lines), Item.Hash,
                   "{0} sym".format(Item.Symbols), Item.Purpose] for Item in Folder.Sources]
    Widths = ColumnWidths(SourceRows)
    Lines += [Row(Entry, Widths) for Entry in SourceRows]
    Lines.append("")

    Ordered = []
    for Entry in Folder.Symbols:
        if Entry.Section not in Ordered:
            Ordered.append(Entry.Section)

    HeaderRows = [[
        "{0} {1}".format(Entry.Role, Entry.Qualified),
        Entry.File,
        SpanText(Entry),
        ",".join(Entry.Tags) or Absent,
        Entry.Cost,
        Sanitised(Entry.Purpose),
    ] for Entry in Folder.Symbols]
    Widths = ColumnWidths(HeaderRows) if HeaderRows else []

    for Section in Ordered:
        Lines += Banner(Section, SectionWidth, "-")
        Lines.append("")
        for Entry, Columns in zip(Folder.Symbols, HeaderRows):
            if Entry.Section != Section:
                continue
            Lines.append(Row(Columns, Widths))
            Lines += DetailLines(Entry)
            Lines.append("")
    while Lines and not Lines[-1]:
        Lines.pop()
    return "\n".join(Lines) + "\n"


def DetailLines(Entry):
    """Indented `key` lines beneath a record header. Emitted only when they carry information."""
    Keyed = []
    for Item in Entry.Inputs:
        Keyed.append(("in", [Item.Name, Item.Declared, Item.Unit, Item.Description]))
    for Item in Entry.Outputs or ([Parameter(Name=Absent, Declared=Entry.Returns, Description=Unknown)]
                                  if Entry.Role == "F" and Entry.Returns not in (Absent, "") else []):
        Keyed.append(("out", [Absent, Item.Declared if Item.Declared != Absent else Entry.Returns,
                              Item.Unit, Item.Description]))
    for Item in Entry.Members:
        Keyed.append(("has", [Item.Name, Item.Declared, Item.Unit, Item.Description]))

    Output = []
    if Keyed:
        Widths = ColumnWidths([Columns for _, Columns in Keyed])
        for Key, Columns in Keyed:
            Output.append(DetailRow(Key, Columns, Widths))
    if Entry.Precondition:
        Output.append(DetailNote("pre", Entry.Precondition))
    if Entry.Postcondition:
        Output.append(DetailNote("post", Entry.Postcondition))
    if Entry.Failure:
        Output.append(DetailNote("err", Entry.Failure))
    if Entry.Uses:
        Output.append(DetailNote("use", ", ".join(Entry.Uses)))
    if Entry.CallSites:
        Output.append(DetailNote("by", ", ".join(Entry.CallSites)))
    for Note in Entry.Notes:
        Output.append(DetailNote("note", Note))
    return Output


def EmitLayerIndex(LayerName, LayerPath, Folders):
    """One line per symbol across the whole layer, plus a pointer to each folder index."""
    Symbols = [(Folder, Entry) for Folder in Folders for Entry in Folder.Symbols]
    Purpose = next((Folder.Purpose for Folder in Folders if Folder.Purpose != Unknown), Unknown)
    Lines = FileHeader(LayerName + IndexSuffix, "Symbol roll for {0} — {1}".format(LayerName, Purpose))
    Lines += Directives([
        ("format", "symbolindex " + FormatVersion),
        ("scope", "layer"),
        ("path", LayerPath),
        ("folders", str(len(Folders))),
        ("symbols", str(len(Symbols))),
    ])
    Lines.append("")

    Lines += Banner("Folder Indexes", SectionWidth, "-")
    Lines.append("")
    ReferenceRows = [["I " + Folder.Name,
                      "{0}/{1}{2}".format(Folder.Name, Folder.Name, IndexSuffix),
                      "{0} sym".format(len(Folder.Symbols)),
                      Folder.Purpose] for Folder in Folders]
    Widths = ColumnWidths(ReferenceRows)
    Lines += [Row(Entry, Widths) for Entry in ReferenceRows]
    Lines.append("")

    Lines += Banner("Symbols", SectionWidth, "-")
    Lines.append("")
    SymbolRows = [["{0} {1}".format(Entry.Role, Entry.Qualified),
                   "{0}/{1}".format(Folder.Name, Entry.File),
                   SpanText(Entry),
                   Sanitised(Entry.Purpose)] for Folder, Entry in Symbols]
    Widths = ColumnWidths(SymbolRows)
    Lines += [Row(Entry, Widths) for Entry in SymbolRows]
    return "\n".join(Lines) + "\n"


def EmitRootIndex(RootName, RootPath, Folders):
    """One line per source folder. Read this first, then exactly one layer, then exactly one folder."""
    Lines = FileHeader(RootName + IndexSuffix,
                       "Root symbol index — every source folder in {0}, one line each.".format(RootPath))
    Layers = []
    for Folder in Folders:
        if Folder.Layer not in Layers:
            Layers.append(Folder.Layer)
    Lines += Directives([
        ("format", "symbolindex " + FormatVersion),
        ("scope", "root"),
        ("path", RootPath),
        ("layers", str(len(Layers))),
        ("folders", str(len(Folders))),
        ("symbols", str(sum(len(Folder.Symbols) for Folder in Folders))),
        ("protocol", "root → layer → folder; never open a source file the folder index already answers"),
    ])
    Lines.append("")

    Rows = [["I " + Folder.Name,
             Folder.Layer or Absent,
             "{0}/{1}{2}".format(Folder.Path, Folder.Name, IndexSuffix),
             "{0} src".format(len(Folder.Sources)),
             "{0} sym".format(len(Folder.Symbols)),
             Folder.Purpose] for Folder in Folders]

    for Layer in Layers:
        Lines += Banner(Layer or "ROOT", SectionWidth, "-")
        Lines.append("")
        Widths = ColumnWidths([Entry for Folder, Entry in zip(Folders, Rows) if Folder.Layer == Layer])
        Lines += [Row(Entry, Widths) for Folder, Entry in zip(Folders, Rows) if Folder.Layer == Layer]
        Lines.append("")
    while Lines and not Lines[-1]:
        Lines.pop()
    return "\n".join(Lines) + "\n"


#--------------------------------------------------------------------------------------------------------------------
#                                                   INDEX READING
#--------------------------------------------------------------------------------------------------------------------

def ReadIndex(FullPath):
    """Round-trip an emitted index back into (Directives, Records). Comment and banner lines are dropped."""
    Settings = {}
    Records = []
    Current = None
    for Number, Line in enumerate(ReadTextFile(FullPath).splitlines(), start=1):
        Stripped = Line.strip()
        if not Stripped or Stripped.startswith("//"):
            continue
        if Stripped.startswith("%"):
            Pieces = Stripped[1:].split(None, 1)
            Settings[Pieces[0]] = Pieces[1].strip() if len(Pieces) > 1 else ""
            continue
        if Line.startswith("    "):
            if Current is not None:
                Current["Details"].append(Stripped)
            continue
        Fields = [Field.strip() for Field in Stripped.split("|")]
        Head = Fields[0].split(None, 1)
        if len(Head) != 2 or Head[0] not in RecordCategories:
            raise ValueError("{0}:{1}: unknown record kind in {2!r}".format(FullPath, Number, Stripped))
        Current = {"Role": Head[0], "Name": Head[1], "Fields": Fields[1:], "Details": [], "Line": Number}
        Records.append(Current)
    return Settings, Records


def IndexFiles(Root):
    Found = []
    for Current, Children, Files in os.walk(Root):
        Children[:] = sorted(Name for Name in Children if Name not in SkippedFolders)
        for Name in sorted(Files):
            if Name.endswith(IndexSuffix):
                Found.append(os.path.join(Current, Name))
    return Found


#--------------------------------------------------------------------------------------------------------------------
#                                                    CONSTRUCTION
#--------------------------------------------------------------------------------------------------------------------

def ConstructIndexes(Root, CallSitesEnabled=True):
    """Scan Root and return (Folders, PlannedFiles, Problems). Nothing is written here."""
    Problems = []
    Folders = []
    TokenSets = {}

    for FolderPath, Sources in SourceFolders(Root):
        Record = FolderRecord(Name=os.path.basename(FolderPath) or os.path.basename(Root),
                              Path=RelativePath(Root, FolderPath),
                              Layer=LayerOf(Root, FolderPath))
        for FileName in Sources:
            SourceEntry, Harvested, Clean = HarvestFile(os.path.join(FolderPath, FileName), FileName, Problems)
            Record.Sources.append(SourceEntry)
            Record.Symbols.extend(Harvested)
            TokenSets[ShortCallSite(Root, FolderPath, FileName)] = set(IdentifierPattern.findall(Clean))
        Record.Purpose = next((Item.Purpose for Item in Record.Sources if Item.Purpose != Unknown), Unknown)
        Folders.append(Record)

    if CallSitesEnabled:
        ResolveCallSites(Folders, TokenSets)

    Planned = {}
    for Folder in Folders:
        Target = os.path.join(Root, os.path.relpath(Folder.Path, os.path.basename(Root)),
                              Folder.Name + IndexSuffix)
        Planned[os.path.normpath(Target)] = EmitFolderIndex(Folder)

    Layers = {}
    for Folder in Folders:
        Layers.setdefault(Folder.Layer, []).append(Folder)
    for LayerName, Members in Layers.items():
        if not LayerName:
            continue
        Target = os.path.join(Root, LayerName, LayerName + IndexSuffix)
        Planned[os.path.normpath(Target)] = EmitLayerIndex(
            LayerName, "{0}/{1}".format(os.path.basename(Root), LayerName), Members)

    RootName = os.path.basename(os.path.abspath(Root))
    Planned[os.path.normpath(os.path.join(Root, RootName + IndexSuffix))] = EmitRootIndex(
        RootName, RootName, Folders)

    return Folders, Planned, Problems


#--------------------------------------------------------------------------------------------------------------------
#                                                     COMMANDS
#--------------------------------------------------------------------------------------------------------------------

def CommandBuild(Options):
    Folders, Planned, Problems = ConstructIndexes(Options.Root, not Options.NoCallSites)
    Written = 0
    for Target, Text in sorted(Planned.items()):
        Existing = ReadTextFile(Target) if os.path.exists(Target) else None
        if Existing != Text:
            WriteTextFile(Target, Text)
            Written += 1
            print("wrote   {0}".format(Target))
    Stale = [Path for Path in IndexFiles(Options.Root) if os.path.normpath(Path) not in Planned]
    for Path in Stale:
        print("orphan  {0}  (source folder no longer exists)".format(Path))

    Total = sum(len(Folder.Symbols) for Folder in Folders)
    Missing = sum(1 for Folder in Folders for Entry in Folder.Symbols if not Entry.Annotated)
    print("\n{0} folders · {1} symbols · {2} unannotated · {3} files written".format(
        len(Folders), Total, Missing, Written))
    for Problem in Problems:
        print("⚠️  {0}".format(Problem))
    return 1 if Problems else 0


def CommandCheck(Options):
    """A stale index is worse than no index. This rebuilds in memory and refuses to agree with disk."""
    _, Planned, Problems = ConstructIndexes(Options.Root, not Options.NoCallSites)
    Differences = []
    for Target, Text in sorted(Planned.items()):
        if not os.path.exists(Target):
            Differences.append("missing  {0}".format(Target))
        elif ReadTextFile(Target) != Text:
            Differences.append("stale    {0}".format(Target))
    for Path in IndexFiles(Options.Root):
        if os.path.normpath(Path) not in Planned:
            Differences.append("orphan   {0}".format(Path))
    for Path in IndexFiles(Options.Root):
        try:
            ReadIndex(Path)
        except ValueError as Failure:
            Differences.append("invalid  {0}".format(Failure))

    for Entry in Differences:
        print("🔴 " + Entry)
    for Problem in Problems:
        print("⚠️  {0}".format(Problem))
    if not Differences:
        print("🟢 every index matches its sources")
    return 1 if Differences else 0


def CommandFind(Options):
    Pattern = re.compile(Options.Pattern, re.I)
    Hits = 0
    for Path in IndexFiles(Options.Root):
        Settings, Records = ReadIndex(Path)
        for Record in Records:
            if Record["Role"] in ("S", "I") and not Options.All:
                continue
            if not Pattern.search(Record["Name"]):
                continue
            Hits += 1
            print("{0}:{1}  {2} {3} | {4}".format(
                Path.replace("\\", "/"), Record["Line"], Record["Role"], Record["Name"],
                " | ".join(Record["Fields"])))
    if not Hits:
        print("no symbol matches {0!r} — the index may not be built; run `build`".format(Options.Pattern))
    return 0 if Hits else 1


def CommandRead(Options):
    """Print the full record and the exact source range, so a source read is bounded to those lines."""
    Wanted = Options.Symbol
    Bare = Wanted.split("::")[-1]
    Hits = 0
    for Path in IndexFiles(Options.Root):
        Settings, Records = ReadIndex(Path)
        if Settings.get("scope") != "folder":
            continue
        for Record in Records:
            if Record["Role"] in ("S", "I"):
                continue
            if Record["Name"] != Wanted and Record["Name"].split("::")[-1] != Bare:
                continue
            Hits += 1
            Folder = Settings.get("path", os.path.dirname(Path))
            FileName = Record["Fields"][0] if Record["Fields"] else "?"
            Span = Record["Fields"][1] if len(Record["Fields"]) > 1 else "?"
            print("{0} {1} | {2}".format(Record["Role"], Record["Name"], " | ".join(Record["Fields"])))
            for Detail in Record["Details"]:
                print("    " + Detail)
            print("    source  {0}/{1} lines {2}".format(Folder, FileName, Span))
            print("")
    if not Hits:
        print("{0!r} is not in any folder index".format(Wanted))
    return 0 if Hits else 1


def CommandStats(Options):
    Folders, _, _ = ConstructIndexes(Options.Root, False)
    Rows = [["folder", "symbols", "annotated", "unannotated"]]
    for Folder in Folders:
        Annotated = sum(1 for Entry in Folder.Symbols if Entry.Annotated)
        Rows.append([Folder.Path, str(len(Folder.Symbols)), str(Annotated),
                     str(len(Folder.Symbols) - Annotated)])
    Widths = ColumnWidths(Rows)
    for Entry in Rows:
        print(Row(Entry, Widths))
    return 0


def Main(Arguments=None):
    Parser = argparse.ArgumentParser(
        prog="SymbolIndex",
        description="Build, verify and query the .symbolindex three-level symbol index.")
    Parser.add_argument("--root", dest="Root", default="Engine",
                        help="folder to scan and index (default: Engine)")
    Parser.add_argument("--no-call-sites", dest="NoCallSites", action="store_true",
                        help="skip the repository-wide `by` sweep")
    Commands = Parser.add_subparsers(dest="Command", required=True)

    Commands.add_parser("build", help="regenerate every index file")
    Commands.add_parser("check", help="fail when any index disagrees with its sources")
    Commands.add_parser("stats", help="annotation coverage per folder")

    Finder = Commands.add_parser("find", help="locate a symbol by regular expression")
    Finder.add_argument("Pattern")
    Finder.add_argument("--all", dest="All", action="store_true", help="include source and reference rows")

    Reader = Commands.add_parser("read", help="print one symbol's full record and its source range")
    Reader.add_argument("Symbol")

    Options = Parser.parse_args(Arguments)
    if not os.path.isdir(Options.Root):
        print("🔴 no such folder: {0}".format(Options.Root))
        return 2

    Handlers = {"build": CommandBuild, "check": CommandCheck, "find": CommandFind,
                "read": CommandRead, "stats": CommandStats}
    return Handlers[Options.Command](Options)


if __name__ == "__main__":
    sys.exit(Main())
