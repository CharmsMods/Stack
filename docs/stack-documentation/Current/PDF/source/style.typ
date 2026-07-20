#let stack-ink = white
#let stack-muted = white
#let stack-paper = rgb("#343434")
#let stack-accent = rgb("#f1dec0")
#let stack-soft = rgb("#444444")
#let stack-line = rgb("#747474")

#let stack-manual(title: "Stack Manual", subtitle: none, body) = {
  set document(title: title, author: "Stack")
  set page(
    paper: "us-letter",
    fill: stack-paper,
    margin: (x: 1.05in, top: 0.92in, bottom: 0.82in),
    header: none,
    footer: context [
      #set text(font: "New Computer Modern", size: 9pt, fill: stack-muted)
      #align(center)[#counter(page).display("1")]
    ],
  )

  set text(font: "New Computer Modern", size: 10.5pt, fill: stack-ink)
  set par(leading: 0.72em, spacing: 0.82em)
  set heading(numbering: "I")
  set list(indent: 1.2em, body-indent: 0.5em, spacing: 0.42em)
  set enum(indent: 1.2em, body-indent: 0.5em, spacing: 0.42em)
  set figure(gap: 0.7em)

  show heading.where(level: 1): it => {
    page(header: none, footer: none, margin: 0pt, fill: stack-paper)[
      #align(center + horizon)[
        #set text(font: "New Computer Modern", fill: stack-ink)
        #align(center)[
          #text(size: 16pt, weight: 400, fill: stack-accent)[#counter(heading).display(it.numbering)]
          #v(0.9em)
          #text(size: 24pt, weight: 700)[#it.body]
        ]
      ]
    ]
  }

  show heading.where(level: 2): it => {
    block(above: 1.25em, below: 0.45em)[
      #text(size: 12pt, weight: 700, fill: stack-ink)[#it.body]
    ]
  }

  show link: set text(fill: stack-accent)
  show figure.caption: set text(size: 8.5pt, fill: stack-muted)

  body
}
