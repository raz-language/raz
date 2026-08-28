# Raz Web

Raz Web is Raz's browser and static-site target. It is not a separate language: `.rz` files use the normal Raz frontend, type system, ownership rules, HIR, MIR, packages, diagnostics, and `.rz` module system.

Raz supports two web authoring layers that share the same compiler and browser WebAssembly backend:

1. **Static-first pages** are the default and the recommended starting point. They emit ordinary HTML that can be hosted anywhere, add no JavaScript for static routes, and compile Raz event/application logic to WebAssembly only when needed.
2. **Reactive components** are the advanced layer for applications that need persistent state, derived state, component rerender boundaries, routing, HTTP/JSON resources, and application-style browser rendering. This layer is imported explicitly as `web::ui`.

The two layers are complementary. A site does not need to opt into the reactive runtime merely to write HTML in Raz.

## Static-first projects

`raz new my-site --web` creates the canonical static-first project. The older `--target web` spelling remains accepted for compatibility. The manifest opts into the web target and the built-in web library:

```toml
[build]
target = "web"

[dependencies]
web = "raz:web"
```

A fresh scaffold contains `src/main.rz`, `public/styles.css`, and `public/favicon.svg`. The starter is deliberately static: `raz build --release` produces normal static-hosting output under `dist/` without JavaScript or WebAssembly. HTML is generated at build time and `public/` assets are copied as ordinary files. Browser behavior added later is compiled to `app.wasm`; Raz application logic is never lowered to JavaScript. Interactive pages may contain a small generated `app.js` host shim whose responsibilities are loading WebAssembly, wiring DOM events to exported Raz functions, and implementing browser host bindings requested by the WebAssembly module.

### Reusable components without a browser runtime

Static pages can reuse the same `Component` tree model as interactive applications without opting into JavaScript or WebAssembly. The static route helper renders the component at build time and rejects browser-bound state/events rather than silently changing the deployment model:

```raz
import web;
import web::ui;

fn hero() -> Component {
    Component section = component(Tag::Section);
    section.class_name("hero");
    section.h1("Hello from Raz");
    section.p("This component becomes ordinary HTML.");
    section.css_class("hero", "max-width: 64rem; margin: 3rem auto;");
    return move section;
}

fn main() -> i64 {
    Component home = hero();
    if (!web::write_component_route("/", "My Raz Site", &mut home)) { return 1; }
    return 0;
}
```

`write_component_route` creates the conventional `dist/` route path, renders the component tree and extracted CSS, and emits no browser runtime. `write_component_route_lang` additionally sets the document language. For richer document metadata, assets, redirects, or progressive browser bindings, create a `Page` and call `page.component(&mut component)` before `write_route`.

This makes `Component` a reusable HTML composition model rather than a synonym for "Wasm application". Only returning `Component` directly from `main` selects the application lowering path.

### Ordinary Raz event handlers

```raz
import web;

global mut i64 count = 0;

fn increment() {
    count += 1;
    web::set_text_i64("count", count);
}

fn main() -> i64 {
    if (!web::prepare_dist()) { return 1; }

    Page page = Page::new("Counter");
    page.p_id("count", "0");
    page.button_id("increment", "Increment");
    page.on_click("increment", "increment");

    if (!page.write_route("/")) { return 1; }
    return 0;
}
```

`increment` is an ordinary Raz function. For the browser artifact it is exported from `app.wasm`. The generated host shim attaches the DOM click listener and invokes that export. `web::set_text_i64` crosses the `raz_web` WebAssembly import boundary to update the DOM.

### Static routes

`Page::write_route` emits host-friendly paths: `/` becomes `dist/index.html`, `/about` becomes `dist/about/index.html`, and so on. A route with no browser bindings emits no `app.js` reference.

### Routing and static prerendering

Raz uses the same route-pattern syntax for build-time prerendering and browser-side matching. Static projects can start from a pattern such as `/blog/:year/:slug` or `/docs/*path`, create an owned route path with `web::route_path`, bind named parameters with `web::route_bind` / `web::route_bind_splat`, and emit it with `Page::write_route_path` or `web::write_component_route_path`. Unresolved dynamic segments and unsafe traversal/query/hash/backslash values are rejected before any file is written. Static routes remain ordinary `dist/.../index.html` files and do not enable JavaScript or WebAssembly.

Interactive applications keep the proven `web::ui` router: `route_match`, `route_is`, route parameter/splat extraction, History API navigation, and normal-link fallback. The shared pattern syntax means a route known at build time can be prerendered without creating a second routing language. Redirects remain standards-based static documents, and `Page::write_not_found` emits the conventional `dist/404.html`.

## Reactive component projects

Application-style UI uses the same normal web target. Import the advanced component layer and return a `Component` from `main`; the compiler selects the reactive lowering automatically:

```toml
[package]
name = "counter-app"
version = "0.1.0"
kind = "executable"
source = "src"
entry = "src/main.rz"

[build]
target = "web"

[dependencies]
web = "raz:web"
```

Legacy `kind = "web"` manifests remain accepted for compatibility, but new projects should use `[build] target = "web"`.

A reactive entry point returns a `Component`. Reusable scoped components can keep natural local state names without colliding with sibling instances:

```raz
import web::ui;

fn Counter(string key, string label) -> Component {
    Component card = component_scoped(Tag::Section, key);
    StateI64 count = card.state_i64("count", 0);

    card.h2(label);
    card.text_i64_state(Tag::P, "Count: ", &mut count);
    card.button_increment("add", "Increment", "+1", &mut count);
    card.button_decrement("subtract", "Decrement", "-1", &mut count);
    return move card;
}

fn main() -> Component {
    Component page = component(Tag::Main);
    Component left = Counter("left-counter", "Left");
    Component right = Counter("right-counter", "Right");
    page.child(&mut left);
    page.child(&mut right);
    return move page;
}
```

`card.state_i64`, `state_bool`, and `state_string` namespace state with the component's stable key-derived scope. Identically named local state in another component instance therefore remains independent. Bound-only state updates use the browser binding fast path and avoid a structural rerender; structural reads are still tracked so Raz can conservatively rerender when markup genuinely depends on state.

The `web::ui` layer includes component/element construction, scoped components and styles, integer/bool/string state, derived integer state, event dispatch, browser routing helpers, HTTP GET/POST requests, JSON decode wrappers, and typed resource state. The compiler emits the browser bundle under `dist/`. Debug builds keep developer-friendly canonical names such as `assets/app.js`, `assets/app.css`, and `assets/app.wasm`; release builds fingerprint generated assets under `dist/assets/` and write `asset-manifest.json`.

## Development server

Run any Raz web project with:

```text
raz dev
```

The command performs an initial debug web build, serves `dist/`, watches the project for changes, and rebuilds after a short debounce. HTML responses receive a development-only reload client; generated deployment files on disk remain normal web output. A failed rebuild keeps the last successful bundle available and shows a small build-failed banner in the browser until the next successful rebuild.

The default endpoint is `http://127.0.0.1:3000`. Override it with:

```text
raz dev --host=0.0.0.0 --port=8080
```

Unknown extensionless URLs fall back to `dist/index.html` so History-API routes work during development. Actual missing assets still return 404. `raz dev` intentionally uses the debug/unfingerprinted asset layout; production fingerprinting remains a `raz build --release` concern.

## Browser standard library

Web projects do **not** depend on the native `std` library. The toolchain ships a separate browser standard-library surface under `library/web/std/`.

- `web::std::dom` — DOM operations exposed through the browser WebAssembly ABI.
- `web::std::events` — browser event vocabulary, including typed input, keyboard, and pointer event views.
- `web::std::timers` — `set_timeout`/`clear_timeout` for exported Raz callbacks.
- `web::std::browser` — browser-runtime capability surface.
- `web::ui` — optional reactive/component application layer.


Typed event views read from the event currently being dispatched into Raz WebAssembly. `InputEvent` exposes form value/checked state, `KeyboardEvent` exposes `key`, `code`, repeat and modifier state, and `PointerEvent` exposes button and viewport client coordinates. General `Event` handlers can prevent default behavior or stop propagation. DOM helpers cover text/attributes/classes plus input values, focus/blur, programmatic clicks, and id existence checks.

Timer callbacks are named exported Raz functions. Because callback names used only by a browser timer are not otherwise visible to tree shaking, register them on the page with `page.browser_export("handler")`; event-bound handlers are rooted automatically by `Page::on`.

Build-time helpers used while generating static files live separately under `library/web/build/`. They are not browser APIs. This keeps filesystem/process/thread/network facilities from the native `std` package out of the browser dependency graph.

## Deployment model

Both modes deploy as normal files. A CDN, object store, GitHub Pages-style service, traditional HTTP server, or any host capable of serving HTML/assets can host the result. Server-side Raz is not required. WebAssembly is used for client logic when the page needs it; static content remains static HTML/CSS.

## Forms and progressive enhancement

Static-first projects can emit ordinary browser-native forms without enabling JavaScript or WebAssembly. Use `Page::form_get`, `Page::form_post`, or `Page::form_multipart` together with the typed field helpers:

```raz
Page page = Page::new("Contact");
page.form_post("/contact", "contact-form");
page.label_for("email", "Email");
page.input_email("email", "you@example.com");
page.select_name("plan", "plan-select");
page.option("free", "Free");
page.option_selected("pro", "Pro");
page.end_select();
page.input_checkbox("terms", "yes", false);
page.input_submit("Send");
page.end_form();
```

File-upload forms use `form_multipart` plus `input_file`. `field_error` emits an accessible `role="alert"`/`aria-live="polite"` status node. These helpers generate ordinary escaped HTML and therefore preserve normal browser GET/POST behavior and work on any host or backend that accepts the form action.

Reactive `web::ui::Element` forms use the same HTML concepts through typed attributes such as `action`, `method`, `enctype`, `required`, `pattern`, `accept`, `multiple`, `selected`, and `checked`. Existing state-bound inputs and Raz-owned submit handlers can opt into the WebAssembly runtime when application behavior actually requires it.

## Static files and content-addressed assets

Raz Web treats `public/` as the project's static-file root. Files such as
`public/favicon.svg`, `public/robots.txt`, and `public/site.webmanifest` keep
stable names and are copied directly into `dist/`.

Files placed below `public/assets/` opt into the production asset pipeline. In
normal development builds they keep their logical paths. In release builds Raz
fingerprints their final bytes, rewrites matching references in HTML, CSS, and
JavaScript, removes the unhashed intermediates, and records the mapping in
`dist/asset-manifest.json`.

For example, author code may refer to `/assets/images/hero.png` and
`/assets/fonts/site.woff2`. A release build can ship them as
`assets/images/hero.<hash>.png` and `assets/fonts/site.<hash>.woff2`. CSS is
rewritten before its own fingerprint is computed, so changing an image updates
both the image URL and any fingerprinted stylesheet that references it. This
makes the output safe for long-lived CDN caching without requiring a Node.js
bundler or runtime.

### Release browser-loader hardening

Interactive release builds compact only the browser glue generated by the Raz compiler. Debug builds keep the generated loader readable for diagnostics, and JavaScript supplied through `[web].javascript` is appended unchanged rather than rewritten by the compactor. The browser loader uses `WebAssembly.instantiateStreaming` when the host serves the Wasm MIME type correctly and falls back to buffered instantiation for ordinary static hosts that do not. Event delegation also uses one root click listener for both Raz actions and client-side route links.

### Release bundle analysis and lazy Wasm chunks

Use `raz build --release --analyze` to inspect the **final deployable tree** after minification, content fingerprinting, canonical-asset removal, and manifest generation. Raz prints a deterministic file/category breakdown and writes the same report to `target/release/web-bundle-analysis.txt`. The report is deliberately outside `dist/`, so analysis never changes deployable bytes. It includes the logical-to-fingerprinted asset map and totals for HTML, CSS, generated JavaScript, WebAssembly, metadata, and static/public assets.

Browser release builds can emit **multiple independent lazy Wasm chunks** in one application. Each chunk is rooted from the handlers assigned to that chunk, emitted independently, fingerprinted independently under `assets/chunks/`, and rewritten into the generated browser host. A lazy-only page does not need a monolithic `app.wasm`.

### Usage-driven browser Wasm imports

Interactive web modules keep stable logical host ABI identifiers internally, but release emission compacts the physical `raz_web` import table to the browser calls reachable from the application's web roots. A component that only updates an integer text node therefore imports only `dom_set_text_i64` instead of the complete DOM/events/storage/history/timer surface. Defined-function, closure, table, and export indices are based on the compact host-import count, so pruning does not change Raz-visible behavior. The generated JavaScript host object may expose additional capabilities, but WebAssembly does not declare or instantiate unused browser imports.

## Direct reactive updates

Raz web keeps application state in the Wasm instance and updates the DOM without a virtual DOM. Explicit text and form bindings are indexed by stable state slot. Derived integer bindings now expose their source-state dependency through the Wasm ABI as well, so a state event refreshes only derived DOM bindings that depend on the changed slot rather than rescanning every computed binding on the page.

Structural dependencies remain conservative. `Component::when` and `Component::unless` are scoped conditional reads: on a keyed component they record the state dependency against that component boundary, allowing Raz to rerender only that subtree when the condition changes. `Component::child_keyed` and `child_keyed_i64` attach stable reconciliation keys to dynamic children; the browser host reorders existing keyed nodes instead of replacing unrelated siblings.

These mechanisms are complementary: bound-only state stays on the direct DOM fast path, while state that changes markup is rerendered at the smallest proven component scope. Page-level rerender remains the fallback when ownership is ambiguous.

## Effects and render lifecycle

Raz Web effects use the same versioned state and scoped structural dependency model as rendering. There is no callback registry, hook-order contract, virtual DOM scheduler, or JavaScript-owned effect graph.

A scoped component can test `first_render()` for one-time initialization during the lifetime of the Wasm instance. `effect_i64`, `effect_bool`, and `effect_string` return true on first observation and whenever that state slot's version changes. Observing the state as an effect also marks it as a structural dependency of the component, so a later change rerenders the smallest proven owner before the guarded effect body runs again. `effect_request` and `Resource<T>::changed_in` apply the same rule to HTTP response generations.

```raz
Component panel = component_scoped(Tag::Section, "settings");
StateI64 count = panel.state_i64("count", 0);

if (panel.first_render()) {
    web::std::storage::set("settings-mounted", "1");
}
if (panel.effect_i64(&mut count)) {
    web::std::storage::set("count-changed", "1");
}
```

Effect bodies are ordinary Raz code executed as part of the owning render. They are intended for browser/IO side effects whose dependency is explicit. State mutation from an effect should be used carefully because changing the same dependency from its own effect can intentionally create another render.

`render_generation()` exposes the current full-render generation for diagnostics and advanced coordination. It is not a replacement for state versions and should not normally be used as a dependency.
