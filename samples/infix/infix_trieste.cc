#include "infix.h"
#include "trieste/trieste.h"

int main(int argc, char** argv)
{
  return trieste::Driver(infix::reader(), nullptr).run(argc, argv);
}
