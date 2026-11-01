export declare function foldAccents(text: string): string;
export declare function tokenize(text: string): string[];

export declare class LemmaIndex {
  constructor();
  addDocument(id: number | string, text: string): void;
  search(query: string): Array<{ id: number | string; score: number }>;
}

export declare class LemmaLexicalAxis {
  readonly name: string;
  constructor();
  registerText(ref: number, text: string): void;
  fill(params: { term: string }): number[];
  rank(params: { term: string }, ref: number): number;
}
