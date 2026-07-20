#import "../style.typ": stack-accent, stack-muted

// Shared layout for dense operation-reference pages. Keep formulas at or below
// body-text scale so the catalog reads as a reference, not as display math.
#let catalog-entry(name, formula, purpose) = grid(
  columns: (0.68in, 0.88in, 1fr),
  column-gutter: 0.08in,
  align(left + top)[
    #text(size: 7.8pt, weight: 600)[#name]
  ],
  align(left + top)[
    #set text(size: 8.1pt)
    #formula
  ],
  align(left + top)[
    #text(size: 7.8pt)[#purpose]
  ],
)

#let catalog-column(spacing: 0.52em, ..entries) = [
  #grid(
    columns: (0.68in, 0.88in, 1fr),
    column-gutter: 0.08in,
    text(size: 6.7pt, weight: 700, fill: stack-accent)[OPERATION],
    text(size: 6.7pt, weight: 700, fill: stack-accent)[FORMULA],
    text(size: 6.7pt, weight: 700, fill: stack-accent)[USE],
  )
  #v(0.38em)
  #stack(dir: ttb, spacing: spacing, ..entries)
]
