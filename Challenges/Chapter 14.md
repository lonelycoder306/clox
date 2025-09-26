## 1
This was slightly challenging to implement, and I considered a few different solutions (none besides this being any good), and I eventually settled on a fairly plain associative array (implemented using two concurrently-updated arrays rather than a typical hash-table/map approach).

We create a new dynamic array wrapper called ```LineArray```. However, this will actually hold two arrays internally, ```lines``` and ```offsets``` both of which are updated in an identical manner (so we only need to track a single count and capacity for both). Instead of storing an ```int``` array in our chunk, we instead store a ```LineArray``` object.

Of course, since we define a new wrapper, we have to chug through the usual initialization and clean-up/deallocation functions, which are nothing new.

The first interesting part happens in our function ```insertLine```, which we call to add the line of the byte we just appended to our chunk.\
If the line we are attempting to insert is the same as the last line saved in the ```lines ``` element of our ```LineArray```, we simply change the offset at the last position in the ```offsets``` element. This will make it so that if we encounter consecutive bytes (whether instructions or operands) on the same line, we simply save the offset in the chunk of the last byte. We use this later to fetch the line.
``` c
if ((array->count > 0) && 
    (line == array->lines[array->count - 1]))
    {
        array->offsets[array->count - 1] = offset;
        return;
    }
```
If the line passed to ```insertLine``` is not the same as the last line we saved, we simply add it to the ```lines``` element and add the offset of the related byte to our ```offsets``` element. Of course, we also have to handle any possible size issues if the dynamic arrays in our ```LineArray``` object are not large enough for a new item to be inserted.
``` c
if (array->capacity < array->count + 1)
{
    int oldCapacity = array->capacity;
    array->capacity = GROW_CAPACITY(oldCapacity);
    array->lines = GROW_ARRAY(int, array->lines, oldCapacity,
                array->capacity);
    array->offsets = GROW_ARRAY(int, array->offsets, oldCapacity,
                array->capacity);
}

array->lines[array->count] = line;
array->offsets[array->count] = offset;
array->count++;
```
The outcome of all of this is that our ```lines``` element contains all the different lines encountered in the chunk sequentially, and our ```offsets``` field contains, for each line, until which byte it continues in our chunk.\
Thus, for this example program in our ```main()```:
``` c
writeConstant(&chunk, 1, 122);
writeConstant(&chunk, 2, 122);

writeChunk(&chunk, OP_RETURN, 123);

writeConstant(&chunk, 3, 124);
```
The associated arrays would look like this:
```
lines = [122, 123, 124]
offsets = [3, 4, 6]
```
It should be noted that these offsets are not specific to instructions, but include operands as well. Thus, even though our last ```OP_CONSTANT``` opcode is at offset 5, the last byte (its operand) is at offset 6.

Then, in our ```getLine()``` function that we use for our disassembler, we simply check to see which range between two consecutive values in ```offsets``` the offset (of the current instruction being disassembled) falls into, and return the line at the same position as the *higher* offset (remember, the offset value is the *last* byte that lies on the same line, so any other byte on the same line would be at that offset or lower) in ```lines```.\
Just in case the offset is smaller than the last one on the first line (and thus is not between any two offset values in ```offsets```), we handle that case first:
``` c
int getLine(Chunk* chunk, int offset)
{
    int min = chunk->opLines.offsets[0];

    // min is the largest offset for the first line.
    if (offset <= min)
        return chunk->opLines.lines[0];
    
    for (int i = 0; i < chunk->opLines.count - 1; i++)
    {
        if ((offset > chunk->opLines.offsets[i]) &&
            (offset <= chunk->opLines.offsets[i+1]))
                return chunk->opLines.lines[i+1];
    }

    return -1; // Unreachable.
}
```

In our disassembler, we make minor adjustments to use this function instead of directly accessing the former ```lines``` array:
``` c
printf("%04d ", offset);
int line = getLine(chunk, offset);
if (offset > 0 &&
    line == getLine(chunk, offset - 1))
        printf("   | ");
else
    printf("%4d ", line);
```

That's it. We can now more concisely store the lines associated with the instructions in our byte-code chunk (we in fact only store as many lines as there are in the source code) and fetch them pretty easily when and where needed.

## 2
This was fairly easy to implement. We first create our ```writeConstant``` function, which will be the only function we use to append instructions that load literal constants to our chunk of byte-code.
``` c
void writeConstant(Chunk* chunk, Value value, int line);
```

We also add a new opcode:
``` c
OP_CONSTANT,
OP_CONSTANT_LONG,
```

In the implementation of the function, we add the given value constant to our constant pool as usual. Then, if the index we get is larger than 255 (the maximum we can store in a single-byte operand), we instead store the index in big-endian format as three separate bytes (allowing up to 2^24 constants rather than 2^8):
``` c
void writeConstant(Chunk* chunk, Value value, int line)
{
    int index = addConstant(chunk, value);
    if (index > 255)
    {
        writeChunk(chunk, OP_CONSTANT_LONG, line);
        writeChunk(chunk, (uint8_t) ((index >> 16) & 0xff), line);
        writeChunk(chunk, (uint8_t) ((index >> 8) & 0xff), line);
        writeChunk(chunk, (uint8_t) (index & 0xff), line);
    }
    else
    {
        writeChunk(chunk, OP_CONSTANT, line);
        writeChunk(chunk, (uint8_t) index, line);
    }
}
```

We then update our disassembler to handle this new instruction:
``` c
case OP_CONSTANT_LONG:
    return constLongInstruction("OP_CONSTANT_LONG", chunk, offset);
```

This calls a new function, ```constLongInstruction```:
``` c
static int constLongInstruction(const char* name, Chunk* chunk, int offset)
{
    int index = ((chunk->code[offset + 1] << 16) |
                    (chunk->code[offset + 2] << 8) |
                    (chunk->code[offset + 3]));
    printf("%-16s %4d '", name, index);
    printValue(chunk->constants.values[index]);
    printf("'\n");
    return offset + 4;
}
```

We simply reconstruct our index stored in big-endian format from our byte-code, and the remainder of the code is the same as in ```constantInstruction()```.\
We also increment the offset by 4 to skip over the instruction and the three bytes used to encode the constant pool index.

That's all we need. To test it, we simply need to fill up our chunk in main() with enough constants to trigger the use of this second instruction, so we use a loop.
``` c
Chunk chunk;
initChunk(&chunk);

for (int i = 0; i < 300; i++)
    writeConstant(&chunk, i, i*10);

disassembleChunk(&chunk, "test chunk");
freeChunk(&chunk);
```
This of course prints out a large number of lines. The important ones are these two:
```
0510 2550 OP_CONSTANT       255 '255'
0512 2560 OP_CONSTANT_LONG  256 '256'
```
This shows that our function worked. Once we fill up the constant pool with 256 constants (the most we can encode with a single-byte operand, as mentioned above), we switch to the ```OP_CONSTANT_LONG``` opcode (we never switch back for that particular chunk), and our byte-code operates as normal.

## 3