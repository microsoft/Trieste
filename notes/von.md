# Verona Object Notation

Design goals, in order:
- Efficient zero-copy random access.
- Typed.
- Arbitrary graph.
- Efficient to encode.
- Possible to read some values with incomplete data (streaming friendly).
- Bijective human readable format.

Non-goals:
- Updateable or versioning. A consumer may understand a different type table (or type table digest) to indicate a new version, but there is no expectation that an unaware consumer can understand a new version.
- Compression. This can be applied during storage or transport, but it isn't part of the format.

## Types

A type for a file is a 64-bit count of the number of types, followed by an entry for each type. This is followed by a 64-bit index into the type table for the root object type.

A `name` in a type is a 64-bit length followed by a number of bytes equal to the length. A `name` is a valid UTF-8 string. These are used only for human readable accessors.

A type is described as follows:

00000000: void
00000001: bit
00000010: byte
00000011: fixed size vector, followed by a type and 64-bit length
00000100: variable size vector, followed by a type
00000101: variant, followed by a 64-bit option count and a number of types equal to the option count
00000110: record, followed by a 64-bit field count and a number of pairs of `type, name` equal to the field count, where the `name` must not be repeated
00000111: pointer, followed by a 64-bit index into the type table
00001000: type index, followed by a 64-bit index into the type table
001xxxxx: floating point, with a 5 bit size class
010xxxxx: unsigned integer, with a 5 bit size class
011xxxxx: integer, with a 5 bit size class

## File Format

The file begins with a 1-bit indicator whether it is self-describing or if the consumer is expected to understand the type.

Next is a 63-bit file length. All offsets must be less than this length. Offsets are relative to the start of the file, and are encoded differently depending on the size of the file.

If the file length is less than 2^16, then all offsets are 16-bit. If the file length is less than 2^32, then all offsets are 32-bit. Otherwise, all offsets are 64-bit. This unsigned integer size will be called a size_t in the rest of this document.

If the file is self-describing, next is the type table. Otherwise, next is a SHA-256 digest of the type table. The consumer is expected to be able to map this digest to the type table.

The root object follows. This must be a valid object of the root type.

## Values

Void is zero length.

A bit is a single byte with value 0 or 1.

Bytes are encoded directly.

Fixed size vectors are encoded as a series of values of the correct type. A bit vector is packed into the minimal byte count.

Variable size vectors are encoded as a size_t offset into the file. At that location in the file, there is a size_t length followed by a series of values of the correct type.

Variants are encoded as a size_t option index followed by a value of the correct type. This is padded with 0 bits to the size of the largest option. It may be more efficient to encode a variant as a pointer to a variant.

Records are encoded as a series of values of the correct type.

Floating point numbers are encoded as IEEE 754-2019 bit patterns. The size of the bit pattern is taken from the type.

Integers and unsigned integers are directly encoded. The type gives the size of the encoding.

Pointers are encoded as size_t offsets into the file. The object at that offset must be of the type specified by the pointer. If the offset is 0, then the pointer is null.

## Human Readable Format

```ts
types
{
  sha256: array 32 byte
  foobar: record { foo: foo; bar: bar }
  foo: variant { i32; string }
  bar: string
  string: array * byte
  opt_foobar: variant { pointer foobar; void }
}

$1: foobar = { foo = 0 42; bar = $2 }
$2: string = 13:Hello, World!
```
