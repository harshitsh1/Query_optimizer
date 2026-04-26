-- Drop tables if exist
DROP TABLE IF EXISTS takes;
DROP TABLE IF EXISTS teaches;
DROP TABLE IF EXISTS student;
DROP TABLE IF EXISTS instructor;
DROP TABLE IF EXISTS course;
DROP TABLE IF EXISTS department;

-- Create tables
CREATE TABLE department (
    dept_name VARCHAR(50) PRIMARY KEY,
    building VARCHAR(15),
    budget NUMERIC(12,2)
);

CREATE TABLE course (
    course_id VARCHAR(8) PRIMARY KEY,
    title VARCHAR(50),
    dept_name VARCHAR(50) REFERENCES department(dept_name),
    credits NUMERIC(2,0)
);

CREATE TABLE instructor (
    ID VARCHAR(5) PRIMARY KEY,
    name VARCHAR(20) NOT NULL,
    dept_name VARCHAR(50) REFERENCES department(dept_name),
    salary NUMERIC(8,2)
);

CREATE TABLE student (
    ID VARCHAR(5) PRIMARY KEY,
    name VARCHAR(20) NOT NULL,
    dept_name VARCHAR(50) REFERENCES department(dept_name),
    tot_cred NUMERIC(3,0)
);

CREATE TABLE teaches (
    ID VARCHAR(5) REFERENCES instructor(ID),
    course_id VARCHAR(8) REFERENCES course(course_id),
    sec_id VARCHAR(8),
    semester VARCHAR(6),
    year NUMERIC(4,0),
    PRIMARY KEY (ID, course_id, sec_id, semester, year)
);

CREATE TABLE takes (
    ID VARCHAR(5) REFERENCES student(ID),
    course_id VARCHAR(8) REFERENCES course(course_id),
    sec_id VARCHAR(8),
    semester VARCHAR(6),
    year NUMERIC(4,0),
    grade VARCHAR(2),
    PRIMARY KEY (ID, course_id, sec_id, semester, year)
);

-- Insert dummy data (expanded slightly so ANALYZE gives > 0 rows)
INSERT INTO department VALUES 
('Comp. Sci.', 'Taylor', 100000),
('Biology', 'Watson', 90000),
('Physics', 'Watson', 70000),
('History', 'Painter', 50000),
('Finance', 'Painter', 120000),
('Music', 'Packard', 80000),
('Elec. Eng.', 'Taylor', 85000);

INSERT INTO course VALUES 
('CS-101', 'Intro. to Computer Science', 'Comp. Sci.', 4),
('CS-190', 'Game Design', 'Comp. Sci.', 4),
('CS-315', 'Robotics', 'Comp. Sci.', 3),
('CS-347', 'Database System Concepts', 'Comp. Sci.', 3),
('BIO-101', 'Intro. to Biology', 'Biology', 4),
('BIO-301', 'Genetics', 'Biology', 4),
('PHY-101', 'Physical Principles', 'Physics', 4),
('HIS-351', 'World History', 'History', 3),
('FIN-201', 'Investment Banking', 'Finance', 3),
('MU-199', 'Music Video Production', 'Music', 3),
('EE-181', 'Introduction to Digital Systems', 'Elec. Eng.', 3);

INSERT INTO instructor VALUES 
('10101', 'Srinivasan', 'Comp. Sci.', 65000),
('12121', 'Wu', 'Finance', 90000),
('15151', 'Mozart', 'Music', 40000),
('22222', 'Einstein', 'Physics', 95000),
('32343', 'El Said', 'History', 60000),
('45565', 'Katz', 'Comp. Sci.', 75000),
('58583', 'Califieri', 'History', 62000),
('76543', 'Singh', 'Finance', 80000),
('76766', 'Crick', 'Biology', 72000),
('83821', 'Brandt', 'Comp. Sci.', 92000),
('98345', 'Kim', 'Elec. Eng.', 80000);

INSERT INTO student VALUES 
('00128', 'Zhang', 'Comp. Sci.', 102),
('12345', 'Shankar', 'Comp. Sci.', 32),
('19991', 'Brandt', 'History', 80),
('23121', 'Chavez', 'Finance', 110),
('44553', 'Peltier', 'Physics', 56),
('45678', 'Levy', 'Physics', 46),
('54321', 'Williams', 'Comp. Sci.', 54),
('55739', 'Sanchez', 'Music', 38),
('70557', 'Snow', 'Physics', 0),
('76543', 'Brown', 'Comp. Sci.', 58),
('76653', 'Aoi', 'Elec. Eng.', 60),
('98765', 'Bourikas', 'Elec. Eng.', 98),
('98988', 'Tanaka', 'Biology', 120);

INSERT INTO teaches VALUES 
('10101', 'CS-101', '1', 'Fall', 2017),
('10101', 'CS-315', '1', 'Spring', 2018),
('10101', 'CS-347', '1', 'Fall', 2017),
('12121', 'FIN-201', '1', 'Spring', 2018),
('22222', 'PHY-101', '1', 'Fall', 2017),
('32343', 'HIS-351', '1', 'Spring', 2018),
('45565', 'CS-101', '1', 'Spring', 2018),
('45565', 'CS-315', '1', 'Spring', 2018),
('76766', 'BIO-101', '1', 'Summer', 2017),
('76766', 'BIO-301', '1', 'Summer', 2018),
('83821', 'CS-190', '1', 'Spring', 2017),
('83821', 'CS-190', '2', 'Spring', 2017),
('83821', 'CS-315', '1', 'Spring', 2018);

INSERT INTO takes VALUES 
('00128', 'CS-101', '1', 'Fall', 2017, 'A'),
('00128', 'CS-347', '1', 'Fall', 2017, 'A-'),
('12345', 'CS-101', '1', 'Fall', 2017, 'C'),
('12345', 'CS-190', '2', 'Spring', 2017, 'A'),
('12345', 'CS-315', '1', 'Spring', 2018, 'A'),
('12345', 'CS-347', '1', 'Fall', 2017, 'A'),
('19991', 'HIS-351', '1', 'Spring', 2018, 'B'),
('23121', 'FIN-201', '1', 'Spring', 2018, 'C+'),
('44553', 'PHY-101', '1', 'Fall', 2017, 'B-'),
('45678', 'PHY-101', '1', 'Fall', 2017, 'F'),
('45678', 'PHY-101', '1', 'Spring', 2018, 'B+'),
('54321', 'CS-101', '1', 'Fall', 2017, 'A-'),
('54321', 'CS-190', '2', 'Spring', 2017, 'B+'),
('55739', 'MU-199', '1', 'Spring', 2018, 'A-'),
('76543', 'CS-101', '1', 'Fall', 2017, 'A'),
('76543', 'CS-315', '1', 'Spring', 2018, 'A'),
('76653', 'EE-181', '1', 'Spring', 2017, 'C'),
('98765', 'CS-101', '1', 'Fall', 2017, 'C-'),
('98765', 'CS-315', '1', 'Spring', 2018, 'B'),
('98988', 'BIO-101', '1', 'Summer', 2017, 'A'),
('98988', 'BIO-301', '1', 'Summer', 2018, null);

-- Build statistics
ANALYZE department;
ANALYZE course;
ANALYZE instructor;
ANALYZE student;
ANALYZE teaches;
ANALYZE takes;
