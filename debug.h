#ifndef DEBUG_H
#define DEBUG_H

#define DEBUG 0

#if DEBUG
  #define DBG_BEGIN(x)    Serial.begin(x)
  #define DBG_PRINT(x)    Serial.print(x)
  #define DBG_PRINTLN(x)  Serial.println(x)
  #define DBG_NEWLINE()   Serial.println()
  #define DBG_FLUSH()     Serial.flush()
#else
  #define DBG_BEGIN(x)
  #define DBG_PRINT(x)
  #define DBG_PRINTLN(x)
  #define DBG_NEWLINE()
  #define DBG_FLUSH()
#endif

#endif
