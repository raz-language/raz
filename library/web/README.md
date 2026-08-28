# Raz Web

`web` is Raz's standard web authoring library. A project selects the web backend with:

```toml
[build]
target = "web"

[dependencies]
web = "raz:web"
```

## Static-first pages

Ordinary `Page` code writes normal HTML that can be deployed to any static host. Text and attribute values are escaped by default.

```raz
import web;

fn main() -> i64 {
    if (!web::prepare_dist()) { return 1; }

    Page page = Page::new("Documentation");
    page.description("Raz documentation");
    page.stylesheet("/site.css");

    page.header("site-header");
    page.nav("nav");
    page.a("/", "Home");
    page.a("/docs", "Docs");
    page.end_nav();
    page.end_header();

    page.main("page");
    page.article("docs");
    page.h1("Raz Web");
    page.p("This route compiles to ordinary static HTML.");
    page.end_article();
    page.end_main();

    if (!page.write_route("/")) { return 1; }
    return 0;
}
```

Static routes do not emit JavaScript or WebAssembly unless browser events are bound.

### Static component composition

`Page` and `Component` can be composed directly. This lets a static site use reusable structured components while preserving the zero-runtime output contract:

```raz
import web;
import web::ui;

fn hero() -> Component {
    Component content = component(Tag::Section);
    content.h1("Raz Web");
    content.p("Reusable component, plain HTML output.");
    return move content;
}

fn main() -> i64 {
    Component content = hero();
    if (!web::write_component_route("/", "Raz Web", &mut content)) { return 1; }
    return 0;
}
```

The static component path rejects state/event/router bindings and raw component client scripts. It never silently adds JavaScript or WebAssembly. Use a `main() -> Component` entry point when the component genuinely needs the interactive application runtime. A richer static document can use `Page::component` together with normal metadata and asset helpers.

## Attributes and semantic HTML

Common semantic elements have direct helpers. For less common HTML or custom elements, `element`, `element_attr`, `open`, and `open_attr` provide an escape hatch while preserving value escaping. HTML tag and attribute names are validated before output.

`raw()` is the explicit opt-out when deliberately authored raw HTML is required.

## CSS

External stylesheets remain ordinary HTML links with `page.stylesheet(...)`. Static CSS can also be authored in Raz and is extracted to `app.css` beside the generated route; this does not enable JavaScript or WebAssembly.

```raz
page.css(":root", "color-scheme: light dark;");
page.css_variable("accent", "#6d28d9");
page.css_class("card", "padding: 1rem; border-radius: 0.75rem;");
page.css_id("search", "min-width: 18rem;");
page.css_pseudo(".card", "hover", "transform: translateY(-1px);");
page.css_pseudo_element(".card", "before", "content: '';");
page.css_media("(max-width: 640px)", ".card", "padding: .75rem;");
page.css_container("(max-width: 30rem)", ".card", "display: block;");
page.css_keyframes("fade-in", "from { opacity: 0; } to { opacity: 1; }");
page.scoped_style("docs", "p", "line-height: 1.6;");
```

Structured rules are deduplicated for both `Page` and `Component` output. `scoped_style` targets an element carrying the corresponding `data-raz-scope` value. First-class helpers cover pseudo-classes (`css_pseudo`), pseudo-elements (`css_pseudo_element`), media queries (`css_media`), container queries (`css_container`), custom properties (`css_variable`), and keyframes (`css_keyframes`). `raw_css` remains the explicit escape hatch for CSS syntax that does not need a dedicated helper. Styles are owned by the Page/Component that declares them: an unused component is never rendered, so its CSS never enters the generated stylesheet. Release bundling deterministically minifies and fingerprints the resulting ordinary CSS; no runtime CSS-in-JS layer exists.

## Browser standard library

Interactive Raz Web applications use thin, typed wrappers around browser standards. These APIs are Wasm imports implemented by the generated host shim; they do not introduce a framework-owned DOM or reactive graph. Static component/page output remains runtime-free unless browser behavior is explicitly used.

The core browser surface includes `web::std::dom`, `events`, `storage`, `location`, `url`, `fetch`, `encoding`, `history`, `clipboard`, and `timers`. DOM helpers support read-side access to element text, value, and attributes in addition to focused writes. `location` exposes the current `href`, `origin`, `pathname`, query string, and hash. `url` provides current-query lookup plus URI-component encode/decode helpers. `fetch` is a thin facade over the Wasm-owned HTTP/resource engine and supports GET plus text-body POST/PUT/PATCH and DELETE, response text/status helpers, typed `Resource<T>` decoding, and coarse network/HTTP error classification. `encoding` exposes the browser UTF-8 boundary and URI-component helpers. Storage includes both persistent local storage and tab/session-scoped storage. Timers support one-shot timeouts and repeating intervals.

Browser APIs intentionally map closely to platform semantics rather than hiding them behind a framework abstraction. Permission-sensitive operations such as clipboard access continue to return success/failure so ordinary Raz control flow can handle browser policy.

## Browser behavior

Bind an element ID to an ordinary Raz function with `on_click`, `on_input`, or `on`. The compiler roots browser WebAssembly at the bound handlers rather than compiling the entire native page generator into the browser module.

## Components

`web::ui` provides the stateful `Component`/`Element` layer. Static-only pages should prefer `Page`; component applications use the same semantic HTML vocabulary and are selected automatically by the web lowering pipeline when `main()` returns `Component`.

Scoped components also provide component-local state namespaces:

```raz
Component card = component_scoped(Tag::Section, "account-card");
StateI64 visits = card.state_i64("visits", 0);
StateBool expanded = card.state_bool("expanded", false);
StateString name = card.state_string("name", "Raz");
```

The same local names can be reused by another component key without sharing slots. This keeps reusable components self-contained while preserving Raz Web's direct-binding fast path for state that only updates text or form controls.

## Full HTML authoring surface

`Page` covers the common document, semantic, text, list, table, form, media, accessibility, and SVG vocabulary directly. Static authoring remains escaped and runtime-free.

For uncommon or custom markup, safe generic helpers avoid raw HTML even when several attributes are required:

```raz
page.element_attr2("p", "data-kind", "note", "aria-label", "Note", "Important");
page.open_attr3("section", "id", "api", "class", "docs", "data-version", "1");
page.text("Escaped content");
page.close("section");
```

Head helpers include ordinary metadata plus `meta_property`, `keywords`, `robots`, `alternate`, `preload`, `manifest`, `canonical`, icons, and stylesheets. Media helpers include eager/lazy/sized images, `<source>`, `<track>`, and `<iframe>`. `landmark(...)` emits id/class/role/ARIA labels together for accessible regions. `svg(...)` and the safe generic element/attribute helpers support inline SVG without requiring a separate template language.

`raw()` remains the explicit escape hatch for intentionally authored raw markup; ordinary APIs escape text and attribute values by default.

## Routing

Raz uses one route model for prerendered pages and interactive browser navigation.
Static pages use `Page::write_route` (plus `write_redirect` and `write_not_found`), while
interactive components read the browser path with `route_is`, `route_match`,
`route_param`, and `route_splat`. Named segments such as `/blog/:slug` capture one
segment; a final named splat such as `/docs/*path` captures the remaining suffix.
`Component::nav_link` emits ordinary links that progressively use the History API
when the interactive runtime is present, so the same URLs still work on a normal
static host.

## HTML5 form controls

Raz Web supports progressive static forms and reactive form elements with modern browser-native input types and validation attributes. Static `Page` helpers include URL, telephone, date/month/week/time, local datetime, color, and range inputs; `Element` also provides typed constructors such as `email_input()`, `number_input()`, `checkbox_input()`, and `submit_input()` so common form controls do not need a separate manual `input_type(...)` call. `label_for(...)` connects an accessible label to a control id, and `option_value(...)` creates escaped `<option>` values.

An ordinary semantic `Element` can be appended directly to a `Component` with `component.element(...)`; keyed variants are available for list/reconciliation cases. This keeps labels, inputs, textareas, selects, tables, and media lightweight without forcing every leaf node to become its own component. Accessibility helpers include generic `aria(...)`, `aria_describedby(...)`, `aria_controls(...)`, boolean `aria_expanded(...)` / `aria_hidden(...)`, root `data(...)`, and `tab_index(...)`. These remain ordinary HTML semantics and do not require JavaScript unless the application adds reactive behavior.

## Lazy browser modules and chunk boundaries

Raz Web keeps client-side module loading independent from the WebAssembly runtime. `Page::lazy_module_on(...)` and `Page::lazy_module_on_click(...)` attach a one-shot dynamic `import()` to an element. If a page only uses these client-module helpers, the build emits a tiny `app.js` loader and **does not emit `app.wasm`**.

Place production chunks under `public/assets/` so release builds fingerprint them and rewrite the generated dynamic-import URL automatically:

```raz
Page page = Page::new("Editor");
page.button_id("open-editor", "Open editor");
page.lazy_module_on_click("open-editor", "/assets/chunks/editor.mjs");
page.write_route("/");
```

`module_preload(...)` can opt a module into early browser fetch, while `module_script(...)` emits a standard immediate `<script type="module">`. These are standards-based ES modules; Raz-WASM function graph splitting remains a separate compiler optimization and is not required for client-only chunks.

### Lazy Raz-WASM chunks

Static-first pages can split independent browser handler graphs into on-demand WebAssembly chunks. `lazy_wasm_on(...)` and `lazy_wasm_on_click(...)` bind a DOM event to a named chunk and an ordinary zero-argument Raz handler:

```raz
page.lazy_wasm_on_click("open-editor", "editor", "open_editor");
```

Handlers assigned to the same chunk are emitted together; functions reachable only from those handlers stay in that chunk. The current qualified backend supports one lazy Raz-WASM chunk per static-first build (with any number of handlers in it); a second distinct chunk is rejected rather than silently merged. A lazy-only page does not emit `app.wasm`. Release builds fingerprint chunks under `assets/chunks/` and rewrite the generated loader automatically. Use normal `on(...)` when a handler belongs in the eagerly loaded main browser module.

## Reactive values

Raz Web keeps reactivity explicit and dependency-indexed rather than using a virtual DOM. `StateI64`, `StateBool`, and `StateString` are stable Wasm-owned slots. `DerivedI64` provides computed integer values, while `DerivedBool` provides computed conditions such as `derived_eq`, `derived_lt`, `derived_gte`, and constant comparison variants such as `derived_gte_const`. Integer derived helpers also include state-to-state add/subtract and constant add/subtract/multiply.

A component reads a computed condition with `when_derived` / `unless_derived` or composes it directly with `child_when_derived` and keyed variants. Those reads propagate structural ownership to the original source state slots, so changing either source rerenders the smallest keyed component that observed the condition. Direct text bindings remain targeted DOM updates.

Async `Resource<T>` values use the same ownership model. `loading_in(component)`, `done_in(component)`, `ready_in(component)`, and `failed_in(component)` mark the underlying HTTP request as a structural dependency of that component scope, so request completion does not require a page-wide rerender when one component owns the result.

## Component composition

Raz Web components remain ordinary typed Raz code. Function parameters are the
component's typed inputs; there is no separate props runtime or template
language. `fragment()` provides wrapper-free composition, while semantic root
constructors such as `main_component()`, `section_component()`, and
`article_component()` avoid repetitive `component(Tag::...)` boilerplate.
Static component trees still emit HTML/CSS only; using fragments or typed
component functions does not introduce JavaScript or WebAssembly. For scoped
interactive components, `child_when(...)`, `child_unless(...)`, and their keyed
variants combine conditional composition with structural dependency tracking,
so toggling the condition can rerender the owning component boundary rather
than conservatively rebuilding the whole page.
