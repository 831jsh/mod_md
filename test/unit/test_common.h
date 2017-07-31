/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Common headers and declarations needed by most/all test source files.
 */

#include <apr.h>   /* for pid_t on Windows, needed by Check */
#include <check.h>

/*
 * A list of Check test case declarations, usually one per source file. Add your
 * test case here when adding a new source file, then add it to the
 * main_test_suite() in main.c.
 */

TCase *md_json_test_case(void);
TCase *md_util_test_case(void);
