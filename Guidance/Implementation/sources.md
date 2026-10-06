# Research notes

Optional background, consulted on 2026-09-29. The guidance combines the user's requirements, inspected Stack code and these primary sources. It is a local synthesis, not an adoption of another project's architecture or entire rule set.

- David L. Parnas, [On the Criteria To Be Used in Decomposing Systems into Modules](https://www.cs.lafayette.edu/~gexia/cs301/resources/parnas.html), 1972, university-hosted reproduction of the paper. Supports choosing boundaries around decisions that should remain private and considering the cost of communication between modules. Applied in `boundaries.md`; it does not prescribe a file count or class hierarchy.
- Bjarne Stroustrup and Herb Sutter, [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines), especially Interfaces, Source files and Performance. Supports explicit contracts, visible dependencies and responsible resource use. Its introduction also discusses gradual application and limits of universal rules. Applied selectively; this guide does not import every language-style rule or forbid Stack's legitimate shared services.
- Qt, [Model/View Programming](https://doc.qt.io/qt-6/model-view-programming.html). A concrete example of separating data from its presentation and using the same data through multiple views. Informs `ui-and-processing.md`; no Qt migration, prescribed MVC classes or signal framework is implied.
- SQLite, [Atomic Commit](https://www.sqlite.org/atomiccommit.html). Illustrates why a reliable save needs a commit and recovery strategy with explicit filesystem assumptions. Informs `persistence.md`; it does not establish Stack's durability guarantees or require a database, SQLite's implementation or its exhaustive crash-testing method.

No claim is made about Lightroom's proprietary internals. The existing `AGENT RULES` folder was not used as source material for this guidance.
