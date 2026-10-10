# Resource and token cost display

Studio fetches public OpenAI pricing at startup on a background worker. The
sources are [API pricing](https://developers.openai.com/api/docs/pricing) and
[ChatGPT pricing and credit rates](https://learn.chatgpt.com/docs/pricing).
Requests send no account credentials. Each response is limited to 1 MiB,
redirects are disabled, and timeouts are bounded. A changed or unavailable
table produces an unavailable state rather than an invented tariff.

Only the explicitly labelled Standard API table is parsed. Model identifiers
must match exactly; annotated identifiers and unknown models are not guessed.
The subscription parser retains advertised starting monthly prices and the
separate published Standard credit rates. These do not reveal the account's
invoice, tax, agreement, remaining allowance or included-task cost.

The document ledger is the source for overall assembly tokens. Cached input
is a subset of input, and reasoning is a subset of output; neither is added
twice. Unknown pricing and missing provider usage remain visible as incomplete
coverage. External clients may not report usage at all.

USD values are **current-rate Standard API-equivalent scenarios**, with short
and long context tariffs shown separately. They are not a historical bill or
a guaranteed lower/upper bound. Persisted cumulative receipts do not contain
per-request context tiers, cache-write counts, regional uplifts or speed tiers;
those charges are excluded. Historical receipts are revalued using the fetched
table date. Purchased-credit equivalents use the independent Standard credit
table and are not converted into included subscription allowance or dollars.

`ark-physics-pricing-tests` covers table selection, format failure, unavailable
models, cache/reasoning subsets, independent API/credit coverage and live
fetch when explicitly run with an argument. Normal CTest is offline.
