#include "../include/natives.h"
#include "../include/value.h"
#include "../include/vm.h"
#include <math.h>
#include <time.h>

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