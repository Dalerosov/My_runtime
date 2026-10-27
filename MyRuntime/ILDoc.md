# IL Documentation

## Types
#### Values

| Type  | Description                     |
|-------|---------------------------------|
| i8    | 8-bit signed integer            |
| i16   | 16-bit signed integer           |
| i32   | 32-bit signed integer           |
| i64   | 64-bit signed integer           |
| u8    | 8-bit unsigned integer          |
| u16   | 16-bit unsigned integer         |
| u32   | 32-bit unsigned integer         |
| u64   | 64-bit unsigned integer         |
| f32   | 32-bit floating point           |
| f64   | 64-bit floating point           |
| bool  | Boolean value (`true` / `false`)|
| usize | Pointer-sized unsigned integer  |
| void  | No value / empty type           |

#### Pointers

| Type              | Description                              |
|-------------------|------------------------------------------|
| ptr\<T>         | Raw pointer, just a memory address       |
| ptr\<const T>   | Raw read-only pointer (compile-time only) |
| array\<T>          | Fat pointer (`ptr + length`)             |
| fn(T...) -> T     | Function pointer                         |


#### Declaration

`$[Variable name] : [var type] (optional =>) = [value]`

`$a : i32`
`$b : i32 = 10`
`$c : i32 = 1010b`
`$d : i32 = Ax`

## Basic operations

#### Arithmetic

| Operation | Form            |
|----------|-----------------|
| add      | $r = add $a, $b |
| sub      | $r = sub $a, $b |
| mul      | $r = mul $a, $b |
| div      | $r = div $a, $b |
| mod      | $r = mod $a, $b |
| neg      | $r = neg $a     |

#### Bitwise

| Operation | Form            |
|----------|-----------------|
| and      | $r = and $a, $b |
| or       | $r = or $a, $b  |
| xor      | $r = xor $a, $b |
| not      | $r = not $a     |
| shl      | $r = shl $a, $b |
| shr      | $r = shr $a, $b |

#### Comparison

| Operation | Form            |
|----------|-----------------|
| eq       | $r = eq $a, $b  |
| ne       | $r = ne $a, $b  |
| lt       | $r = lt $a, $b  |
| le       | $r = le $a, $b  |
| gt       | $r = gt $a, $b  |
| ge       | $r = ge $a, $b  |

## Pointers

#### Memory

| Operation | Form              | Description |
|----------|-------------------|-------------|
| alloc    | $p = alloc T      | heap allocation (single object) |
| alloc    | $p = alloc T, $n  | heap allocation (array) |
| stackalloc | $p = stackalloc T | stack allocation (auto freed on return) |
| free     | free $p           | explicit deallocation |
| load     | $v = load $p      | read value from memory |
| store    | store $p, $v     | write value to memory |
| offset   | $p = offset $p, $n | pointer arithmetic |

#### Arrays

| Operation | Form                          | Description |
|----------|-------------------------------|-------------|
| alloc    | $a = alloc_array T, $n        | allocate array |
| load     | $v = array_load $a, $i        | read element |
| store    | array_store $a, $i, $v        | write element |
| len      | $n = array_len $a             | get array length |

#### Structs

| Operation | Form                          | Description |
|----------|-------------------------------|-------------|
| field    | $p = field_ptr $p, <offset>   | pointer to struct field by offset |

#### Casts

| Operation | Form                          | Description |
|----------|-------------------------------|-------------|
| bitcast  | $r = bitcast $a               | reinterpret bits |
| ptr2int  | $n = ptr_to_int $rp           | pointer → integer |
| int2ptr  | $p = int_to_ptr $n            | integer → pointer |

## Control Flow

| Operation | Form                  | Description |
|----------|-----------------------|-------------|
| jmp      | jmp label             | unconditional jump |
| jz       | jz $cond, label       | jump if zero/false |
| jnz      | jnz $cond, label      | jump if non-zero/true |
| call     | $r = call $fn, args...| function call |
| ret      | ret $v                | return from function |

