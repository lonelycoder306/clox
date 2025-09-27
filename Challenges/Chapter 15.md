## 1
#### ```1 * 2 + 3```
```
OP_CONSTANT 1
OP_CONSTANT 2
OP_MULTIPLY
OP_CONSTANT 3
OP_ADD
```

#### ```1 + 2 * 3```
```
OP_CONSTANT 1
OP_CONSTANT 2
OP_CONSTANT 3
OP_MULTIPLY
OP_ADD
```

#### ```3 - 2 - 1```
```
OP_CONSTANT 3
OP_CONSTANT 2
OP_SUBTRACT
OP_CONSTANT 1
OP_SUBTRACT
```

#### ```1 + 2 * 3 - 4 / -5```
```
OP_CONSTANT 1
OP_CONSTANT 2
OP_CONSTANT 3
OP_MULTIPLY
OP_CONSTANT 4
OP_CONSTANT 5
OP_NEGATE
OP_DIVIDE
OP_SUBTRACT
OP_ADD
```

## 2
Without ```OP_NEGATE```:
```
OP_CONSTANT 4
OP_CONSTANT 3
OP_CONSTANT 0
OP_CONSTANT 2
OP_SUBTRACT
OP_MULTIPLY
OP_SUBTRACT
```
We simply change each negation into a subtraction from a "phantom" zero.

Without ```OP_SUBTRACT```:
```
OP_CONSTANT 4
OP_CONSTANT 3
OP_CONSTANT 2
OP_NEGATE
OP_MULTIPLY
OP_NEGATE
OP_ADD
```
We instead represent each subtraction as a negation followed by an addition (since, for example, 2 - 1 is equivalent to 2 + (-1)).

It seems more worthwhile to have both instructions, since the overhead introduced is minimal and reduces the number of confusing cases we have to work through like the above (particularly if we get more gnarly expressions with several negations or subtraction operations).\
If we *had* to get rid of one of the two, however, the better option to remove would be ```OP_SUBTRACT```. It introduces more confusion with the needed phantom zero to mimic negation, and requires us to handle the overhead of using another value in our bytecode, as we have to add it to the constant pool for our chunk, add an extra byte in the bytecode to store its index in the constant pool, and handle adding it and removing it from our stack, in addition to any additional operations those actions need, such as re-sizing the constant pool.\
On the other hand, OP_NEGATE requires no additional values, takes no operands and has relatively little additional overhead.

## 3
This is very easy to implement. We simply multiply the last value on the stack by -1 without popping it.
``` c
case OP_NEGATE: *(vm.stackTop - 1) *= -1; break;
```

## 4
(Review chapter answers.)