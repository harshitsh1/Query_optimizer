
SELECT instructor.name, instructor.salary
FROM instructor
WHERE instructor.salary > 75000

SELECT student.name, course.title
FROM student
JOIN takes ON student.ID = takes.ID
JOIN course ON takes.course_id = course.course_id
WHERE course.dept_name = 'Comp. Sci.'

SELECT department.dept_name FROM department WHERE department.budget - 100000 < 50000

SELECT instructor.name FROM instructor WHERE (instructor.salary * 2) + 5000 > 200000

SELECT customers.id, customers.name, orders.order_id, orders.amount
FROM customers JOIN orders AS o ON customers.id = o.customer_id
JOIN temp ON temp.id = customers.id
WHERE customers.age > 30 AND orders.amount > 100

SELECT instructor.name, instructor.salary
FROM instructor
WHERE instructor.salary > 75000

SELECT student.name, course.title, takes.grade FROM student JOIN takes ON student.ID = takes.ID JOIN course ON takes.course_id = course.course_id WHERE student.tot_cred > 100 AND course.credits = 4

SELECT student.name, takes.course_id FROM student, takes WHERE student.ID = takes.ID AND student.tot_cred > 120

SELECT sub.name, sub.title FROM (SELECT student.name, course.title, course.credits FROM student JOIN takes ON student.ID = takes.ID JOIN course ON takes.course_id = course.course_id) AS sub WHERE sub.credits > 3

SELECT student.name, takes.course_id FROM student, takes WHERE student.ID = takes.ID AND student.tot_cred > 120


