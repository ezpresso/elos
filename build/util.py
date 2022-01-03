import inspect
import sys

class SkipBodyException(Exception): pass
class SkipBody:
	def __init__(self, name=None):
		self.name = name

	def skipbody(self, frame, event, arg):
		raise SkipBodyException()

	def __enter__(self):
		if self.name != None:
			print("skipping " + self.name)

		sys.settrace(lambda *args, **keys: None)
		frame = inspect.currentframe().f_back
		frame.f_trace = self.skipbody
		return self

	def __exit__(self, type, value, traceback):
		if isinstance(value, SkipBodyException):
			return True
		else:
			return value is None

def execfile(path, gobals=None):
	with open(path, 'rb') as file:
		exec(compile(file.read(), path, 'exec'), gobals)
