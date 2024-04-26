#include "shrubbery.h"

int main(int argc, char** argv)
{
  return trieste::Driver(shrubbery::reader()).run(argc, argv);
}
