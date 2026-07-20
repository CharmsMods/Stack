#import "style.typ": stack-manual, stack-ink, stack-paper, stack-accent

#show: stack-manual.with(
  title: "Stack",
  subtitle: "By Charm?",
)

#page(header: none, footer: none, margin: 0pt, fill: stack-paper)[
  #align(center + horizon)[
    #block(width: 66%)[
      #set text(font: "New Computer Modern", fill: stack-ink)
      #align(center)[
        #text(size: 26pt, weight: 400)[Stack]
        #v(0.55em)
        #text(size: 7.5pt, fill: stack-accent)[Charm?]
      ]
    ]
  ]
]

#counter(page).update(1)

#include "contents.typ"
#include "content/outline.typ"
