# FORK-NOTES — mod-ale-chronicle

Fork de **[azerothcore/mod-ale](https://github.com/azerothcore/mod-ale)** (mod-eluna / ALE).

## Raison du fork

Ce fork existe pour porter les besoins du pipeline narratif **Chronicle**, où les
hooks Lua d'ALE déclenchent des appels HTTP sortants vers des backends LLM. Ces
backends ont une latence d'inférence élevée (typiquement 15–25 s par réponse
d'agent), ce qui dépasse largement les timeouts client HTTP par défaut de l'ALE
upstream. Le fork porte le delta minimal nécessaire pour que ces appels longs
aboutissent, sans diverger du reste du code upstream.

## Delta vs upstream

Point de divergence (tip upstream) : `b9c03a3`
(*fix(LuaEngine): Preallocate Lua stack size in PushRefsFor…*).

Commits locaux non-upstream, **delta fonctionnel uniquement** (les commits
docs/Capy/setup_archive sont exclus de cet inventaire) :

| Commit    | Type        | Description |
|-----------|-------------|-------------|
| `3efab5e` | fix         | `fix(LuaEngine/Http): bump httplib client read/write timeout 5s→60s/30s` |
| `53fc181` | merge       | Merge PR locale #1 (`fix/httplib-timeout`) intégrant `3efab5e` |

Commits locaux **exclus** de l'inventaire fonctionnel (purement
infrastructure/configuration de fork, sans impact sur le code ALE) :

- `9721538` — `docs(capy): add fork-local Captain context + canonical CAPY-PLATFORM`
- `98b6e13` — `chore: quarantine Capy .capy/ config to setup_archive/*.bak`

### Détail du delta fonctionnel — `3efab5e`

Fichier touché : `src/LuaEngine/HttpManager.cpp` (4 insertions, 4 suppressions).

Les deux clients `httplib::Client` instanciés dans `HttpManager::HttpWorkerThread()`
(le client initial `cli` et le client de suivi de redirection `cli2`) passent de :

```cpp
cli.set_read_timeout(5, 0);  // 5 seconds
cli.set_write_timeout(5, 0); // 5 seconds
```

à :

```cpp
cli.set_read_timeout(60, 0); // 60 seconds — covers LLM inference + safety margin
cli.set_write_timeout(30, 0); // 30 seconds
```

Le `connection_timeout` (3 s) reste inchangé. Motivation : le `read_timeout` de
5 s tronquait toute réponse HTTP dépassant 5 s, en dessous du plancher de latence
de tout backend non-trivial (inférence LLM, requêtes DB lourdes). En cas de
timeout, `HttpManager` loggue l'erreur et ne pousse pas de réponse dans la queue,
donc le callback Lua ne se déclenche jamais.

## Politique de synchronisation

- **Sync upstream : rebase / merge manuel délibéré.** Aucune synchronisation
  automatique. Les mises à jour upstream sont intégrées à la main, après revue,
  pour préserver la lisibilité du delta fonctionnel ci-dessus.
- Le delta fonctionnel doit rester **minimal et isolé** : un seul fichier
  (`HttpManager.cpp`), aisément rebasable au-dessus de l'upstream.
- Les commits d'infrastructure de fork (docs Capy, setup_archive) sont maintenus
  séparément et ne doivent jamais être proposés à l'upstream.

## Statut d'upstreaming

- **`3efab5e` (timeout httplib) : non-upstreamé.** Préparation d'upstreaming en
  cours — texte prêt-à-poster rédigé dans
  [`docs/upstream-httplib-timeout-proposal.md`](docs/upstream-httplib-timeout-proposal.md).
  L'issue/PR upstream **n'a pas encore été postée**.
- Objectif à terme : faire converger le fork vers zéro delta fonctionnel en
  faisant accepter le fix upstream (idéalement sous une forme de timeout
  configurable par requête, cf. la proposition).
