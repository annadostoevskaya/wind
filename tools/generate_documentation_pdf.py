#!/usr/bin/env python3
"""Render the canonical WIND Markdown guide as a polished, deterministic PDF."""

from __future__ import annotations

import argparse
import hashlib
import html
import re
from pathlib import Path

from reportlab import rl_config

rl_config.invariant = 1

from reportlab.lib import colors
from reportlab.lib.colors import HexColor
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    BaseDocTemplate,
    Flowable,
    Frame,
    HRFlowable,
    KeepTogether,
    NextPageTemplate,
    PageBreak,
    PageTemplate,
    Paragraph,
    Preformatted,
    Spacer,
    Table,
    TableStyle,
)
from reportlab.platypus.tableofcontents import TableOfContents


PAGE_WIDTH, PAGE_HEIGHT = A4
NAVY = HexColor("#0B1F33")
NAVY_2 = HexColor("#12324A")
TEAL = HexColor("#0F766E")
TEAL_LIGHT = HexColor("#D9F3EE")
CYAN = HexColor("#3CC6B7")
INK = HexColor("#18242E")
MUTED = HexColor("#5E6B75")
LINE = HexColor("#D9E2E8")
PAPER = HexColor("#F7F9FA")
WHITE = colors.white


def first_existing(paths: list[Path]) -> Path | None:
    return next((path for path in paths if path.exists()), None)


def register_fonts() -> None:
    windows = Path("C:/Windows/Fonts")
    regular = first_existing([
        windows / "segoeui.ttf",
        Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
    ])
    bold = first_existing([
        windows / "segoeuib.ttf",
        Path("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"),
    ])
    mono = first_existing([
        windows / "consola.ttf",
        Path("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"),
    ])
    mono_bold = first_existing([
        windows / "consolab.ttf",
        Path("/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"),
    ])
    if not all((regular, bold, mono, mono_bold)):
        raise RuntimeError("A Cyrillic-capable sans and monospace font set is required")
    pdfmetrics.registerFont(TTFont("WindSans", str(regular)))
    pdfmetrics.registerFont(TTFont("WindSansBold", str(bold)))
    pdfmetrics.registerFont(TTFont("WindMono", str(mono)))
    pdfmetrics.registerFont(TTFont("WindMonoBold", str(mono_bold)))
    pdfmetrics.registerFontFamily("WindSans", normal="WindSans", bold="WindSansBold")
    pdfmetrics.registerFontFamily("WindMono", normal="WindMono", bold="WindMonoBold")


def inline_markup(text: str) -> str:
    escaped = html.escape(text.strip())
    escaped = re.sub(
        r"\[([^\]]+)\]\(([^)]+)\)",
        r'<link href="\2" color="#0F766E"><u>\1</u></link>',
        escaped,
    )
    escaped = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", escaped)
    return re.sub(
        r"`([^`]+)`",
        r'<font name="WindMono" color="#0F766E" size="8.2">\1</font>',
        escaped,
    )


def visible_length(value: str) -> int:
    return len(re.sub(r"[`*_]", "", value))


def table_widths(rows: list[list[str]], available: float) -> list[float]:
    columns = max(len(row) for row in rows)
    first_header = rows[0][0] if rows and rows[0] else ""
    if columns == 3 and first_header == "Состояние":
        return [42 * mm, 72 * mm, 52 * mm]
    if columns == 3 and first_header == "Симптом":
        return [31 * mm, 68 * mm, 67 * mm]
    if columns == 3 and first_header in {"Bytes", "Status bit"}:
        return [28 * mm, 64 * mm, 74 * mm]
    maxima = []
    for column in range(columns):
        length = max(
            visible_length(row[column]) if column < len(row) else 0 for row in rows
        )
        maxima.append(max(7, min(length, 42)))
    total = float(sum(maxima))
    return [available * value / total for value in maxima]


def split_table_row(line: str) -> list[str]:
    return [cell.strip() for cell in line.strip().strip("|").split("|")]


def is_table_separator(line: str) -> bool:
    cells = split_table_row(line)
    return bool(cells) and all(re.fullmatch(r":?-{3,}:?", cell) for cell in cells)


class SectionRule(Flowable):
    def __init__(self, width: float):
        super().__init__()
        self.width = width
        self.height = 4 * mm

    def draw(self) -> None:
        self.canv.setFillColor(CYAN)
        self.canv.roundRect(0, 1.4 * mm, 23 * mm, 1.2 * mm, 0.6 * mm, fill=1, stroke=0)
        self.canv.setFillColor(LINE)
        self.canv.rect(25 * mm, 1.7 * mm, self.width - 25 * mm, 0.45, fill=1, stroke=0)


class WindDocTemplate(BaseDocTemplate):
    def afterFlowable(self, flowable: Flowable) -> None:
        if not isinstance(flowable, Paragraph):
            return
        if flowable.style.name not in {"WindH1", "WindH2"}:
            return
        level = 0 if flowable.style.name == "WindH1" else 1
        text = flowable.getPlainText()
        digest = hashlib.sha1(f"{level}:{text}".encode("utf-8")).hexdigest()[:12]
        key = f"section-{digest}"
        self.canv.bookmarkPage(key)
        self.canv.addOutlineEntry(text, key, level=level, closed=False)
        if level == 0:
            self.notify("TOCEntry", (level, text, self.page, key))


def draw_cover(canvas, doc) -> None:
    canvas.saveState()
    canvas.setFillColor(NAVY)
    canvas.rect(0, 0, PAGE_WIDTH, PAGE_HEIGHT, fill=1, stroke=0)
    canvas.setFillColor(NAVY_2)
    canvas.circle(PAGE_WIDTH - 24 * mm, PAGE_HEIGHT - 22 * mm, 55 * mm, fill=1, stroke=0)
    canvas.setStrokeColor(CYAN)
    canvas.setLineWidth(1.1)
    for radius in (24, 34, 44):
        canvas.circle(PAGE_WIDTH - 28 * mm, PAGE_HEIGHT - 32 * mm, radius * mm, fill=0, stroke=1)
    canvas.setLineWidth(3)
    hub_x = PAGE_WIDTH - 39 * mm
    hub_y = PAGE_HEIGHT - 44 * mm
    canvas.circle(hub_x, hub_y, 2.2 * mm, fill=0, stroke=1)
    for dx, dy in ((0, 30), (26, -15), (-26, -15)):
        canvas.line(hub_x, hub_y, hub_x + dx * mm, hub_y + dy * mm)
    canvas.setLineWidth(1.2)
    canvas.line(hub_x, hub_y - 2 * mm, hub_x - 7 * mm, 30 * mm)
    canvas.line(hub_x, hub_y - 2 * mm, hub_x + 7 * mm, 30 * mm)
    canvas.setFillColor(CYAN)
    canvas.rect(0, 0, 8 * mm, PAGE_HEIGHT, fill=1, stroke=0)
    canvas.setFillColor(WHITE)
    canvas.setFont("WindSansBold", 8.5)
    canvas.drawString(18 * mm, 17 * mm, "STM32F407VGTx  /  RAK3172  /  LoRaWAN")
    canvas.restoreState()


def draw_body_background(canvas, doc) -> None:
    canvas.saveState()
    canvas.setFillColor(WHITE)
    canvas.rect(0, 0, PAGE_WIDTH, PAGE_HEIGHT, fill=1, stroke=0)
    canvas.restoreState()


def draw_body_chrome(canvas, doc) -> None:
    canvas.saveState()
    canvas.setFillColor(NAVY)
    canvas.rect(0, PAGE_HEIGHT - 8 * mm, PAGE_WIDTH, 8 * mm, fill=1, stroke=0)
    canvas.setFillColor(CYAN)
    canvas.rect(0, PAGE_HEIGHT - 8 * mm, 38 * mm, 8 * mm, fill=1, stroke=0)
    canvas.setFont("WindSansBold", 7.4)
    canvas.setFillColor(WHITE)
    canvas.drawString(18 * mm, PAGE_HEIGHT - 5.4 * mm, "WIND")
    canvas.setFont("WindSans", 7.2)
    canvas.drawString(45 * mm, PAGE_HEIGHT - 5.4 * mm, "ENGINEERING GUIDE")
    canvas.setStrokeColor(LINE)
    canvas.setLineWidth(0.6)
    canvas.line(18 * mm, 13 * mm, PAGE_WIDTH - 18 * mm, 13 * mm)
    canvas.setFillColor(MUTED)
    canvas.setFont("WindSans", 7.2)
    canvas.drawString(18 * mm, 8.5 * mm, "Production firmware documentation")
    canvas.setFont("WindSansBold", 7.4)
    canvas.drawRightString(PAGE_WIDTH - 18 * mm, 8.5 * mm, f"{canvas.getPageNumber():02d}")
    canvas.restoreState()


def build_styles() -> dict[str, ParagraphStyle]:
    body = ParagraphStyle(
        "WindBody", parent=getSampleStyleSheet()["BodyText"], fontName="WindSans",
        fontSize=9.1, leading=12.8, textColor=INK, spaceAfter=2.5 * mm,
        allowWidows=0, allowOrphans=0,
    )
    return {
        "body": body,
        "h1": ParagraphStyle(
            "WindH1", parent=body, fontName="WindSansBold", fontSize=20,
            leading=23, textColor=NAVY, spaceBefore=3 * mm, spaceAfter=2.5 * mm,
            keepWithNext=True,
        ),
        "h2": ParagraphStyle(
            "WindH2", parent=body, fontName="WindSansBold", fontSize=13.2,
            leading=16, textColor=TEAL, spaceBefore=3.2 * mm, spaceAfter=1.7 * mm,
            keepWithNext=True,
        ),
        "h3": ParagraphStyle(
            "WindH3", parent=body, fontName="WindSansBold", fontSize=10.4,
            leading=13, textColor=NAVY_2, spaceBefore=2.5 * mm, spaceAfter=1.2 * mm,
            keepWithNext=True,
        ),
        "bullet": ParagraphStyle(
            "WindBullet", parent=body, fontSize=8.9, leading=12.2,
            leftIndent=1.5 * mm, spaceAfter=0.8 * mm,
        ),
        "code": ParagraphStyle(
            "WindCode", parent=body, fontName="WindMono", fontSize=7.2,
            leading=9.6, textColor=HexColor("#E8F3F5"), leftIndent=4 * mm,
            rightIndent=4 * mm, spaceBefore=1.5 * mm, spaceAfter=2.5 * mm,
        ),
        "callout": ParagraphStyle(
            "WindCallout", parent=body, fontSize=8.8, leading=12.2,
            textColor=NAVY, spaceAfter=0,
        ),
        "cover_title": ParagraphStyle(
            "CoverTitle", parent=body, fontName="WindSansBold", fontSize=47,
            leading=50, textColor=WHITE, alignment=TA_LEFT, spaceAfter=5 * mm,
        ),
        "cover_subtitle": ParagraphStyle(
            "CoverSubtitle", parent=body, fontName="WindSans", fontSize=18,
            leading=24, textColor=HexColor("#C7FFF6"), alignment=TA_LEFT,
            spaceAfter=10 * mm,
        ),
        "cover_meta": ParagraphStyle(
            "CoverMeta", parent=body, fontSize=9.5, leading=15,
            textColor=WHITE, alignment=TA_LEFT, spaceAfter=1 * mm,
        ),
        "toc_title": ParagraphStyle(
            "TocTitle", parent=body, fontName="WindSansBold", fontSize=24,
            leading=28, textColor=NAVY, spaceAfter=8 * mm,
        ),
        "toc0": ParagraphStyle(
            "TOC0", parent=body, fontName="WindSansBold", fontSize=10,
            leading=14, textColor=NAVY, spaceBefore=2 * mm,
        ),
        "toc1": ParagraphStyle(
            "TOC1", parent=body, fontSize=8.5, leading=12,
            textColor=MUTED, leftIndent=7 * mm,
        ),
    }


def make_callout(text: str, styles: dict[str, ParagraphStyle]) -> Table:
    table = Table(
        [["", Paragraph(inline_markup(text), styles["callout"])]],
        colWidths=[2.2 * mm, 164 * mm], hAlign="LEFT",
    )
    table.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (0, -1), TEAL),
        ("BACKGROUND", (1, 0), (1, -1), TEAL_LIGHT),
        ("BOX", (0, 0), (-1, -1), 0.5, HexColor("#B9DDD7")),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (1, 0), (1, -1), 4 * mm),
        ("RIGHTPADDING", (1, 0), (1, -1), 4 * mm),
        ("TOPPADDING", (0, 0), (-1, -1), 3 * mm),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 3 * mm),
    ]))
    return table


def make_code_block(code: str, styles: dict[str, ParagraphStyle]) -> Table:
    pre = Preformatted(code.rstrip(), styles["code"], maxLineLength=96)
    table = Table([[pre]], colWidths=[166 * mm], hAlign="LEFT")
    table.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, -1), NAVY),
        ("BOX", (0, 0), (-1, -1), 0.8, NAVY_2),
        ("LEFTPADDING", (0, 0), (-1, -1), 0),
        ("RIGHTPADDING", (0, 0), (-1, -1), 0),
        ("TOPPADDING", (0, 0), (-1, -1), 2 * mm),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 1 * mm),
    ]))
    return table


def make_markdown_table(rows: list[list[str]], styles: dict[str, ParagraphStyle]) -> Table:
    columns = max(len(row) for row in rows)
    normalized = [row + [""] * (columns - len(row)) for row in rows]
    cell_style = ParagraphStyle(
        "TableCell", parent=styles["body"], fontSize=7.5, leading=9.5, spaceAfter=0,
    )
    header_style = ParagraphStyle(
        "TableHeader", parent=cell_style, fontName="WindSansBold", textColor=WHITE,
    )
    data = [[
        Paragraph(inline_markup(cell), header_style if row_index == 0 else cell_style)
        for cell in row
    ] for row_index, row in enumerate(normalized)]
    table = Table(
        data, colWidths=table_widths(normalized, 166 * mm), repeatRows=1, hAlign="LEFT",
    )
    commands = [
        ("BACKGROUND", (0, 0), (-1, 0), NAVY_2),
        ("GRID", (0, 0), (-1, -1), 0.35, LINE),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 2.2 * mm),
        ("RIGHTPADDING", (0, 0), (-1, -1), 2.2 * mm),
        ("TOPPADDING", (0, 0), (-1, -1), 1.8 * mm),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 1.8 * mm),
    ]
    for row_index in range(1, len(data)):
        if row_index % 2 == 0:
            commands.append(("BACKGROUND", (0, row_index), (-1, row_index), PAPER))
    table.setStyle(TableStyle(commands))
    return table


def make_list(
    items: list[str], styles: dict[str, ParagraphStyle], numbered: bool
) -> Table:
    marker_style = ParagraphStyle(
        "ListMarker",
        parent=styles["bullet"],
        fontName="WindSansBold",
        fontSize=10 if numbered else 8,
        textColor=TEAL,
        alignment=TA_LEFT,
        spaceAfter=0,
    )
    rows = []
    for index, item in enumerate(items, start=1):
        marker = f"{index}" if numbered else "•"
        rows.append([
            Paragraph(marker, marker_style),
            Paragraph(inline_markup(item), styles["bullet"]),
        ])
    table = Table(rows, colWidths=[7 * mm, 159 * mm], hAlign="LEFT")
    table.setStyle(TableStyle([
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 0),
        ("RIGHTPADDING", (0, 0), (-1, -1), 0),
        ("TOPPADDING", (0, 0), (-1, -1), 0.7 * mm),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 0.7 * mm),
    ]))
    return table


def is_special(line: str, next_line: str = "") -> bool:
    stripped = line.strip()
    return (
        not stripped or stripped.startswith("#") or stripped.startswith("```")
        or stripped.startswith(">") or stripped.startswith("<!-- pagebreak")
        or stripped == "---" or re.match(r"^[-*]\s+", stripped) is not None
        or re.match(r"^\d+\.\s+", stripped) is not None
        or (stripped.startswith("|") and is_table_separator(next_line))
    )


def parse_body(markdown: str, styles: dict[str, ParagraphStyle]) -> list[Flowable]:
    lines = markdown.splitlines()
    story: list[Flowable] = []
    index = 0
    while index < len(lines):
        stripped = lines[index].strip()
        if not stripped:
            index += 1
            continue
        if stripped.startswith("<!-- pagebreak"):
            story.append(PageBreak())
            index += 1
            continue
        heading = re.match(r"^(#{2,4})\s+(.+)$", stripped)
        if heading:
            level = len(heading.group(1))
            style = styles[{2: "h1", 3: "h2", 4: "h3"}[level]]
            flowables: list[Flowable] = [Paragraph(inline_markup(heading.group(2)), style)]
            if level == 2:
                flowables.append(SectionRule(166 * mm))
            story.append(KeepTogether(flowables))
            index += 1
            continue
        if stripped.startswith("```"):
            code_lines: list[str] = []
            index += 1
            while index < len(lines) and not lines[index].strip().startswith("```"):
                code_lines.append(lines[index])
                index += 1
            index += 1
            story.extend([make_code_block("\n".join(code_lines), styles), Spacer(1, 1.5 * mm)])
            continue
        if stripped.startswith(">"):
            quote_lines: list[str] = []
            while index < len(lines) and lines[index].strip().startswith(">"):
                quote_lines.append(lines[index].strip()[1:].strip())
                index += 1
            story.extend([make_callout(" ".join(quote_lines), styles), Spacer(1, 2.5 * mm)])
            continue
        if re.match(r"^[-*]\s+", stripped):
            items: list[str] = []
            while index < len(lines):
                match = re.match(r"^[-*]\s+(.+)$", lines[index].strip())
                if not match:
                    break
                item_parts = [match.group(1)]
                index += 1
                while index < len(lines):
                    continuation = lines[index]
                    if not continuation.strip() or not re.match(r"^\s{2,}\S", continuation):
                        break
                    item_parts.append(continuation.strip())
                    index += 1
                items.append(" ".join(item_parts))
            story.extend([make_list(items, styles, numbered=False), Spacer(1, 2 * mm)])
            continue
        if re.match(r"^\d+\.\s+", stripped):
            items: list[str] = []
            while index < len(lines):
                match = re.match(r"^\d+\.\s+(.+)$", lines[index].strip())
                if not match:
                    break
                item_parts = [match.group(1)]
                index += 1
                while index < len(lines):
                    continuation = lines[index]
                    if not continuation.strip() or not re.match(r"^\s{2,}\S", continuation):
                        break
                    item_parts.append(continuation.strip())
                    index += 1
                items.append(" ".join(item_parts))
            story.extend([make_list(items, styles, numbered=True), Spacer(1, 2 * mm)])
            continue
        if (stripped.startswith("|") and index + 1 < len(lines)
                and is_table_separator(lines[index + 1])):
            rows = [split_table_row(lines[index])]
            index += 2
            while index < len(lines) and lines[index].strip().startswith("|"):
                rows.append(split_table_row(lines[index]))
                index += 1
            story.extend([make_markdown_table(rows, styles), Spacer(1, 3 * mm)])
            continue
        if stripped == "---":
            story.extend([
                HRFlowable(width="100%", thickness=0.7, color=LINE),
                Spacer(1, 2 * mm),
            ])
            index += 1
            continue
        paragraph_lines = [stripped]
        index += 1
        while index < len(lines):
            next_line = lines[index + 1] if index + 1 < len(lines) else ""
            if is_special(lines[index], next_line):
                break
            paragraph_lines.append(lines[index].strip())
            index += 1
        story.append(Paragraph(inline_markup(" ".join(paragraph_lines)), styles["body"]))
    return story


def extract_cover(markdown: str) -> tuple[str, str, list[str], str, str]:
    title_match = re.search(r"(?m)^#\s+(.+)$", markdown)
    subtitle_match = re.search(r"(?m)^##\s+(.+)$", markdown)
    title = title_match.group(1).strip() if title_match else "WIND"
    subtitle = subtitle_match.group(1).strip() if subtitle_match else "Engineering guide"
    first_break = markdown.find("<!-- pagebreak -->")
    cover_source = markdown[:first_break] if first_break >= 0 else markdown
    metadata = []
    for line in cover_source.splitlines():
        match = re.match(r"^\*\*(.+?):\*\*\s*(.+?)\s{0,2}$", line.strip())
        if match:
            metadata.append(f"{match.group(1)}: {match.group(2)}")
    quote = " ".join(
        line.strip()[1:].strip() for line in cover_source.splitlines()
        if line.strip().startswith(">")
    )
    body = markdown[first_break + len("<!-- pagebreak -->"):] if first_break >= 0 else markdown
    return title, subtitle, metadata, quote, body


def render(markdown_path: Path, output_path: Path) -> None:
    register_fonts()
    styles = build_styles()
    title, subtitle, metadata, quote, body = extract_cover(
        markdown_path.read_text(encoding="utf-8")
    )
    output_path.parent.mkdir(parents=True, exist_ok=True)
    document = WindDocTemplate(
        str(output_path), pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm,
        topMargin=17 * mm, bottomMargin=17 * mm, title=f"{title} - {subtitle}",
        author="WIND firmware", subject="STM32F407VGTx production firmware guide",
        creator="tools/generate_documentation_pdf.py", pdfVersion=(1, 7),
    )
    cover_frame = Frame(
        18 * mm, 22 * mm, PAGE_WIDTH - 36 * mm, PAGE_HEIGHT - 44 * mm,
        id="cover-frame", showBoundary=0,
    )
    body_frame = Frame(
        18 * mm, 20 * mm, PAGE_WIDTH - 36 * mm, PAGE_HEIGHT - 33 * mm,
        id="body-frame", showBoundary=0,
    )
    document.addPageTemplates([
        PageTemplate(id="cover", frames=[cover_frame], onPage=draw_cover),
        PageTemplate(
            id="body",
            frames=[body_frame],
            onPage=draw_body_background,
            onPageEnd=draw_body_chrome,
        ),
    ])
    story: list[Flowable] = [
        Spacer(1, 44 * mm),
        Paragraph(inline_markup(title), styles["cover_title"]),
        Paragraph(inline_markup(subtitle), styles["cover_subtitle"]),
    ]
    for item in metadata:
        story.append(Paragraph(inline_markup(item), styles["cover_meta"]))
    story.append(Spacer(1, 13 * mm))
    if quote:
        card_style = ParagraphStyle(
            "CoverCard", parent=styles["cover_meta"], fontSize=9, leading=13,
            textColor=HexColor("#DDFBF6"),
        )
        card = Table(
            [[Paragraph(inline_markup(quote), card_style)]],
            colWidths=[122 * mm], hAlign="LEFT",
        )
        card.setStyle(TableStyle([
            ("BACKGROUND", (0, 0), (-1, -1), NAVY_2),
            ("BOX", (0, 0), (-1, -1), 0.8, TEAL),
            ("LEFTPADDING", (0, 0), (-1, -1), 5 * mm),
            ("RIGHTPADDING", (0, 0), (-1, -1), 5 * mm),
            ("TOPPADDING", (0, 0), (-1, -1), 4 * mm),
            ("BOTTOMPADDING", (0, 0), (-1, -1), 4 * mm),
        ]))
        story.append(card)
    toc = TableOfContents()
    toc.levelStyles = [styles["toc0"], styles["toc1"]]
    story.extend([
        NextPageTemplate("body"), PageBreak(),
        Paragraph("Содержание", styles["toc_title"]),
        SectionRule(166 * mm), Spacer(1, 4 * mm), toc, PageBreak(),
    ])
    story.extend(parse_body(body, styles))
    document.multiBuild(story)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path("docs/WIND_FIRMWARE.md"))
    parser.add_argument(
        "--output", type=Path, default=Path("output/pdf/wind-firmware-guide.pdf")
    )
    args = parser.parse_args()
    render(args.source.resolve(), args.output.resolve())
    print(args.output.resolve())


if __name__ == "__main__":
    main()
