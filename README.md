# MadPostgres
Library based on libpq to provide interfacing with postgresql databases.

## API proposal

```madlib
// Define by madpostgres:
type Value
  = StringValue String
  | IntegerValue Integer
  | JsonValue String
  | GeoPositionValue #[Float, FLoat]
  | ...

readString :: Value -> Maybe String
readInteger :: Value -> Maybe Integer





alias Row = List Value

type QueryResult = QueryResult(List Row)
type QueryError = QueryError(..)

connect :: String -> Wish ConnectionError Connection
query :: String -> Connection -> Wish QueryError QueryResult
getRows :: QueryResult -> List Row
mapRow2 :: (Value -> Maybe a) -> (Value -> Maybe b) -> (a -> b -> c) -> Maybe c

// User land
type User
  = User String Integer

pipe(
  Psql.connect,
  chain(Psql.query("select name, age from user")), // we now simply make a query
  map(pipe( // map into the Wish
    getRows,
    nth(0), // get the first row
    chain(mapRow2(readString, readInteger, User))
  ))
)(connectionUrl)
```
