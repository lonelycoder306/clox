#include "../include/natives.h"
#include "../include/value.h"
#include "../include/vm.h"
#include <math.h>
#include <string.h>
#include <time.h>

const int nativesCount = 4;

// Directly initializing each struct with {...}
// didn't work for some reason.
static void fillNatives()
{
    natives[0].obj.type = OBJ_NATIVE;
    natives[0].name = "clock";
    natives[0].function = clockNative;
    natives[0].arity = 0;

    natives[1].obj.type = OBJ_NATIVE;
    natives[1].name = "sqrt";
    natives[1].function = sqrtNative;
    natives[1].arity = 1;

    natives[2].obj.type = OBJ_NATIVE;
    natives[2].name = "type";
    natives[2].function = typeNative;
    natives[2].arity = 1;

    natives[3].obj.type = OBJ_NATIVE;
    natives[3].name = "length";
    natives[3].function = lengthNative;
    natives[3].arity = 1;
}

static void defineNative(ObjNative* nativeFunc)
{
    int index = vm.globalValues.count;
    ObjString* identifier = copyString(nativeFunc->name, (int) strlen(nativeFunc->name));
    writeValueArray(&vm.globalValues, OBJ_VAL(nativeFunc));
    tableSet(&vm.globalNames, OBJ_VAL(identifier), NUMBER_VAL((double)index));
}

void defineNatives()
{
    fillNatives();
    
    for (int i = 0; i < nativesCount; i++)
        defineNative(&natives[i]);
}

Value clockNative(int argCount, Value* args)
{    
    return NUMBER_VAL((double) clock() / CLOCKS_PER_SEC);
}

Value sqrtNative(int argCount, Value* args)
{
    return NUMBER_VAL(sqrt(AS_NUMBER(args[0])));
}

Value typeNative(int argCount, Value* args)
{
    ObjString* typeName = NULL; // Dummy initial value.
    
    switch (args[0].type)
    {
        case VAL_BOOL:
            typeName = copyString("<boolean>", 9);
            break;
        case VAL_NIL:
            typeName = copyString("<nil>", 5);
            break;
        case VAL_NUMBER:
            typeName = copyString("<number>", 8);
            break;
        case VAL_OBJ:
        {
            Obj* object = AS_OBJ(args[0]);

            switch (object->type)
            {
                case OBJ_STRING:
                    typeName = copyString("<string>", 8);
                    break;
                case OBJ_FUNCTION:
                    typeName = copyString("<function>", 10);
                    break;
                case OBJ_NATIVE:
                    typeName = copyString("<builtin function>", 18);
                    break;
            }
            break;
        }
        default:
            ;
    }

    return OBJ_VAL(typeName);
}

Value lengthNative(int argCount, Value* args)
{
    // Assuming that argument is always a string.
    return NUMBER_VAL((double) strlen(AS_CSTRING(args[0])));
}