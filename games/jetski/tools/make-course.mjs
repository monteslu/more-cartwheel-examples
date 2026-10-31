// Makes the courses: every courses/<id>.mjs named (or all of LEVEL_ORDER) to its files under assets/course/<id>/ and its data in
// src/courses/. A new course is a new definition, added to LEVEL_ORDER: tools/course-lib.mjs says what a definition holds.
//   node games/jetski/tools/make-course.mjs [id ...]
import { load, makeCourse, writeIndex } from './course-lib.mjs';
const LEVEL_ORDER = ['atoll', 'sound'];
const only = process.argv.slice(2);
const defs = [];
for (const id of LEVEL_ORDER) {
  const def = await load(id);
  defs.push(def);
  if (!only.length || only.includes(id)) makeCourse(def);
}
writeIndex(defs);
